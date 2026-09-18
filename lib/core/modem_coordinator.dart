import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';
import 'package:path/path.dart' as p;
import 'dart:ui' as ui;
import 'package:uuid/uuid.dart';

import 'ffi_bindings.dart';
import 'raster_bridge.dart';
import '../database/models.dart';
import '../database/app_database.dart';
import '../plugins/modem_plugin.dart';
import '../plugins/implementations/all_plugins.dart';

class CallsignRequiredException implements Exception {
  final String message;
  CallsignRequiredException([this.message = 'A valid amateur radio callsign is legally required before transmitting.']);

  @override
  String toString() => message;
}

class ModemCoordinator {
  static final ModemCoordinator instance = ModemCoordinator._internal();

  PolyglotNativeBindings? _bindings;
  PolyglotNativeBindings get bindings => _bindings ??= PolyglotNativeBindings.instance;
  bool get isNativeReady => _bindings != null;

  late final AppDatabase database;
  bool _isInitialized = false;

  final ReceivePort _eventPort = ReceivePort();
  StreamSubscription? _eventSub;

  final ValueNotifier<StationSettings> settingsNotifier =
      ValueNotifier<StationSettings>(const StationSettings());
  final ValueNotifier<bool> isSentryLockedNotifier = ValueNotifier<bool>(false);
  final ValueNotifier<String?> activeProtocolNotifier = ValueNotifier<String?>(null);
  final ValueNotifier<double> currentSquelchLevelNotifier = ValueNotifier<double>(-90.0);
  final ValueNotifier<bool> isTransmittingNotifier = ValueNotifier<bool>(false);
  final ValueNotifier<bool> isOutputMutedNotifier = ValueNotifier<bool>(false);
  final ValueNotifier<bool> isInputMutedNotifier = ValueNotifier<bool>(false);
  final ValueNotifier<ui.Image?> liveCanvasNotifier = ValueNotifier<ui.Image?>(null);

  bool get isSentryLocked => isSentryLockedNotifier.value;

  void toggleOutputMute() {
    final newMuted = !isOutputMutedNotifier.value;
    isOutputMutedNotifier.value = newMuted;
    try {
      _bindings?.setOutputMuted(newMuted);
    } catch (_) {}
  }

  void toggleInputMute() {
    final newMuted = !isInputMutedNotifier.value;
    isInputMutedNotifier.value = newMuted;
    try {
      _bindings?.setInputMuted(newMuted);
    } catch (_) {}
  }

  // Buffer for active text streams (e.g. CW / PSK31 tokens)
  String? _activeStreamTxId;
  String? _activeImageTxId;
  String? get activeStreamTxId => _activeStreamTxId;
  String? get activeImageTxId => _activeImageTxId;
  bool _hasEmittedTransmissionForCurrentBurst = false;

  final StringBuffer _activeStreamBuffer = StringBuffer();
  String _activeStreamWavPath = '';
  String _storageDirectory = '/tmp/polyglot_recordings';
  String get storageDirectory => _storageDirectory;

  ModemCoordinator._internal();

  Future<void> initialize({
    AppDatabase? customDb,
    bool enableLoopback = false,
  }) async {
    if (_isInitialized) return;

    registerAllDefaultPlugins();
    database = customDb ?? await AppDatabase.openDefault();

    // Load persisted station settings from SQLite
    try {
      final loaded = database.loadStationSettings();
      settingsNotifier.value = enableLoopback
          ? loaded.copyWith(isLoopbackEnabled: true)
          : loaded;
    } catch (_) {}

    try {
      _bindings = PolyglotNativeBindings.instance;
      RasterBridge.instance.initialize();

      // Configure storage directory for audio WAV files
      String audioDir;
      try {
        final appDocs = await getApplicationDocumentsDirectory();
        audioDir = p.join(appDocs.path, 'polyglot_radio', 'recordings');
        final dir = Directory(audioDir);
        if (!dir.existsSync()) dir.createSync(recursive: true);
      } catch (_) {
        audioDir = '/tmp/polyglot_recordings';
        Directory(audioDir).createSync(recursive: true);
      }
      _storageDirectory = audioDir;
      _bindings!.setStorageDirectory(audioDir);

      // Register event port for asynchronous C++ dispatch
      _bindings!.registerEventPort(_eventPort.sendPort.nativePort);
      _eventSub = _eventPort.listen(_handleNativeEvent);

      // Start native audio hardware / loopback HAL
      _bindings!.startAudioHAL(
        sampleRate: 48000,
        enableLoopback: settingsNotifier.value.isLoopbackEnabled,
      );
      _bindings!.setLoopbackMode(settingsNotifier.value.isLoopbackEnabled);
      _bindings!.setSquelchThreshold(settingsNotifier.value.squelchThresholdDb);
    } catch (e, stack) {
      debugPrint("ModemCoordinator native initialization warning: $e\n$stack");
    }

    _isInitialized = true;
  }

  void _handleNativeEvent(dynamic message) {
    if (message is! List || message.length < 5) return;

    final eventTypeInt = message[0] as int;
    final protocolId = message[1] as String;
    final snrDb = (message[2] as num).toDouble();
    final metadataInt = message[3] as int;
    final payload = message[4] as Uint8List;

    final eventType = EventType.fromInt(eventTypeInt);
    final plugin = PluginRegistry.instance.get(protocolId);
    final displayName = plugin?.displayName ?? protocolId;

    switch (eventType) {
      case EventType.carrierDetected:
        isSentryLockedNotifier.value = true;
        activeProtocolNotifier.value = displayName;
        _activeStreamWavPath = utf8.decode(payload, allowMalformed: true);
        _activeStreamTxId = const Uuid().v4();
        _activeImageTxId = _activeStreamTxId;
        _activeStreamBuffer.clear();
        _hasEmittedTransmissionForCurrentBurst = false;

        // Clear live scanline canvas for incoming image reception
        RasterBridge.instance.clearCanvas();
        liveCanvasNotifier.value = null;

        if (plugin?.category == PayloadCategory.image) {
          database.insertTransmission(Transmission(
            id: _activeImageTxId!,
            timestamp: DateTime.now().millisecondsSinceEpoch,
            direction: TransmissionDirection.rx,
            protocolId: protocolId,
            protocolDisplayName: displayName,
            payloadType: PayloadType.image,
            textContent: "Receiving $displayName...",
            snrDb: snrDb,
            durationMs: 0,
            audioFilePath: _activeStreamWavPath,
            isIdentified: true,
            imageFilePath: null,
          ));
        }
        break;

      case EventType.carrierLost:
        isSentryLockedNotifier.value = false;
        activeProtocolNotifier.value = null;

        // If an image stream finished on carrier lost without explicit packetDecoded
        final currentImageTxId = _activeImageTxId;
        if (plugin?.category == PayloadCategory.image && !_hasEmittedTransmissionForCurrentBurst) {
          final fileName = '${protocolId}_${DateTime.now().millisecondsSinceEpoch}.png';
          final imgPath = p.join(_storageDirectory, fileName);
          final capturedWav = _activeStreamWavPath;
          final targetTxId = currentImageTxId ?? const Uuid().v4();
          RasterBridge.instance.decodeCurrentCanvas(
            onFrameReady: (image) async {
              final byteData = await image.toByteData(format: ui.ImageByteFormat.png);
              if (byteData != null) {
                final file = File(imgPath);
                await file.writeAsBytes(byteData.buffer.asUint8List());
                database.insertTransmission(Transmission(
                  id: targetTxId,
                  timestamp: DateTime.now().millisecondsSinceEpoch,
                  direction: TransmissionDirection.rx,
                  protocolId: protocolId,
                  protocolDisplayName: displayName,
                  payloadType: PayloadType.image,
                  textContent: "Scanline Capture ($displayName)",
                  durationMs: metadataInt,
                  audioFilePath: capturedWav,
                  isIdentified: true,
                  imageFilePath: imgPath,
                ));
              }
            },
          );
          _hasEmittedTransmissionForCurrentBurst = true;
        }

        // If we were accumulating a continuous text stream (CW / PSK31 / Hell)
        if (_activeStreamTxId != null) {
          final text = _activeStreamBuffer.toString().trim();
          if (text.isNotEmpty) {
            database.insertTransmission(Transmission(
              id: _activeStreamTxId!,
              timestamp: DateTime.now().millisecondsSinceEpoch,
              direction: TransmissionDirection.rx,
              protocolId: protocolId,
              protocolDisplayName: displayName,
              payloadType: PayloadType.text,
              textContent: text,
              snrDb: snrDb,
              durationMs: metadataInt,
              audioFilePath: _activeStreamWavPath,
              isIdentified: true,
            ));
            _hasEmittedTransmissionForCurrentBurst = true;
          } else if (!_hasEmittedTransmissionForCurrentBurst && _activeStreamWavPath.isNotEmpty) {
            // Identified or captured burst completed without explicit packet decode:
            // Keep the recording and present the transmission card so user can review and re-demodulate
            database.insertTransmission(Transmission(
              id: _activeStreamTxId!,
              timestamp: DateTime.now().millisecondsSinceEpoch,
              direction: TransmissionDirection.rx,
              protocolId: protocolId,
              protocolDisplayName: displayName,
              payloadType: plugin?.category.toPayloadType() ?? PayloadType.unknown,
              textContent: "Audio Burst Captured ($displayName)",
              snrDb: snrDb,
              durationMs: metadataInt,
              audioFilePath: _activeStreamWavPath,
              isIdentified: plugin != null,
            ));
            _hasEmittedTransmissionForCurrentBurst = true;
          }
          _activeStreamTxId = null;
          _activeStreamBuffer.clear();
          _activeStreamWavPath = '';
        }

        _activeImageTxId = null;
        liveCanvasNotifier.value = null;
        break;

      case EventType.textStream:
        final charStr = utf8.decode(payload, allowMalformed: true);
        _activeStreamBuffer.write(charStr);
        break;

      case EventType.packetDecoded:
        final text = utf8.decode(payload, allowMalformed: true);
        final txId = _activeImageTxId ?? const Uuid().v4();

        if (plugin?.category == PayloadCategory.image) {
          final fileName = 'sstv_${DateTime.now().millisecondsSinceEpoch}.png';
          final imgPath = p.join(_storageDirectory, fileName);
          final capturedWav = _activeStreamWavPath;

          RasterBridge.instance.decodeCurrentCanvas(
            onFrameReady: (image) async {
              final byteData = await image.toByteData(format: ui.ImageByteFormat.png);
              if (byteData != null) {
                final file = File(imgPath);
                await file.writeAsBytes(byteData.buffer.asUint8List());
                final existing = database.getTransmissionById(txId);
                if (existing != null) {
                  database.insertTransmission(existing.copyWith(
                    imageFilePath: imgPath,
                    textContent: text.isNotEmpty ? text : existing.textContent,
                  ));
                } else {
                  database.insertTransmission(Transmission(
                    id: txId,
                    timestamp: DateTime.now().millisecondsSinceEpoch,
                    direction: TransmissionDirection.rx,
                    protocolId: protocolId,
                    protocolDisplayName: displayName,
                    payloadType: PayloadType.image,
                    textContent: text.isNotEmpty ? text : "Scanline Capture ($displayName)",
                    rawPayload: payload,
                    snrDb: snrDb,
                    durationMs: metadataInt > 0 ? metadataInt : 1000,
                    audioFilePath: capturedWav,
                    isIdentified: true,
                    imageFilePath: imgPath,
                  ));
                }
              }
            },
          );
        }

        final tx = Transmission(
          id: txId,
          timestamp: DateTime.now().millisecondsSinceEpoch,
          direction: TransmissionDirection.rx,
          protocolId: protocolId,
          protocolDisplayName: displayName,
          payloadType: plugin?.category.toPayloadType() ?? PayloadType.packet,
          textContent: text.isNotEmpty ? text : "Scanline Capture ($displayName)",
          rawPayload: payload,
          snrDb: snrDb,
          durationMs: metadataInt > 0 ? metadataInt : 1000,
          audioFilePath: _activeStreamWavPath,
          isIdentified: true,
          imageFilePath: null,
        );
        database.insertTransmission(tx);
        _hasEmittedTransmissionForCurrentBurst = true;
        break;

      case EventType.rasterLineReady:
        RasterBridge.instance.decodeCurrentCanvas(
          onFrameReady: (image) {
            liveCanvasNotifier.value = image;
          },
        );
        break;

      case EventType.statusUpdate:
        final statusMsg = utf8.decode(payload, allowMalformed: true);
        if (statusMsg.isNotEmpty) {
          activeProtocolNotifier.value = statusMsg;
          if (_activeImageTxId != null) {
            final existing = database.getTransmissionById(_activeImageTxId!);
            if (existing != null) {
              database.insertTransmission(existing.copyWith(
                textContent: "Receiving $statusMsg...",
              ));
            }
          }
        }
        break;
    }
  }

  Future<bool> transmit({
    required String protocolId,
    required String text,
    Uint8List? rawBytes,
    String? imageFilePath,
  }) async {
    final plugin = PluginRegistry.instance.get(protocolId);
    if (plugin == null) return false;

    // LEGAL INVARIANT: Amateur packet modes require valid callsign!
    if (plugin.requiresCallsign) {
      if (!settingsNotifier.value.hasValidCallsign) {
        throw CallsignRequiredException(
          'Protocol "${plugin.displayName}" requires a valid amateur radio callsign before transmitting. Please configure your callsign in Station Settings.',
        );
      }
    }

    Uint8List payload;
    if (plugin.category == PayloadCategory.image && imageFilePath != null && imageFilePath.isNotEmpty) {
      final file = File(imageFilePath);
      if (file.existsSync()) {
        try {
          final fileBytes = file.readAsBytesSync();
          final codec = await ui.instantiateImageCodec(
            fileBytes,
            targetWidth: 320,
            targetHeight: 240,
          );
          final frame = await codec.getNextFrame();
          final byteData = await frame.image.toByteData(format: ui.ImageByteFormat.rawRgba);
          if (byteData != null) {
            payload = byteData.buffer.asUint8List();
          } else {
            payload = fileBytes;
          }
        } catch (_) {
          payload = Uint8List.fromList(utf8.encode(imageFilePath));
        }
      } else {
        // e.g. "synthetic_color_bars.png" or non-existent file: pass empty to trigger native SMPTE color bars
        payload = Uint8List(0);
      }
    } else if (rawBytes != null && rawBytes.isNotEmpty) {
      payload = rawBytes;
    } else if (text.isNotEmpty) {
      payload = Uint8List.fromList(utf8.encode(text));
    } else if (imageFilePath != null && imageFilePath.isNotEmpty) {
      final file = File(imageFilePath);
      if (file.existsSync()) {
        try {
          payload = file.readAsBytesSync();
        } catch (_) {
          payload = Uint8List.fromList(utf8.encode(imageFilePath));
        }
      } else {
        payload = Uint8List.fromList(utf8.encode(imageFilePath));
      }
    } else {
      payload = Uint8List.fromList([0]);
    }
    final configMap = plugin.getDefaultConfig();
    final currentSettings = settingsNotifier.value;
    if (currentSettings.callsign != null) {
      configMap['callsign'] = currentSettings.callsign!;
    }
    if (plugin.id == 'rattlegram') {
      configMap['carrier_freq'] = currentSettings.rattlegramCarrierFreq;
      configMap['sensitivity'] = currentSettings.rattlegramSensitivity;
      configMap['mode'] = currentSettings.rattlegramMode;
    } else if (plugin.id == 'feld_hell') {
      configMap['carrier_freq'] = currentSettings.feldHellCarrierFreq;
      configMap['mode'] = currentSettings.feldHellMode;
    } else if (plugin.id == 'eas_same') {
      configMap['originator'] = currentSettings.easOriginator;
      configMap['event_code'] = currentSettings.easEventCode;
    } else if (plugin.id == 'cw_morse') {
      configMap['pitch'] = currentSettings.cwPitch;
      configMap['wpm'] = currentSettings.cwWpm;
    } else if (plugin.id == 'sstv_engine') {
      configMap['mode'] = currentSettings.sstvMode;
    }
    final jsonConfig = jsonEncode(configMap);

    isTransmittingNotifier.value = true;
    final success = bindings.startTransmit(
      protocolId: plugin.id,
      payload: payload,
      jsonConfig: jsonConfig,
    );

    if (success) {
      // Record outgoing transmission in database
      database.insertTransmission(Transmission(
        id: const Uuid().v4(),
        timestamp: DateTime.now().millisecondsSinceEpoch,
        direction: TransmissionDirection.tx,
        protocolId: protocolId,
        protocolDisplayName: plugin.displayName,
        payloadType: plugin.category.toPayloadType(),
        textContent: text,
        imageFilePath: imageFilePath,
        rawPayload: payload,
        snrDb: 30.0, // Clean internal TX
        durationMs: 2000,
        audioFilePath: 'outbound_tx',
        isIdentified: true,
      ));
    }

    // Monitor transmission completion with a watchdog timeout
    int checkTicks = 0;
    Timer.periodic(const Duration(milliseconds: 100), (timer) {
      checkTicks++;
      if (!bindings.isTransmitting() || checkTicks > 300) { // 30s safety timeout
        isTransmittingNotifier.value = false;
        timer.cancel();
      }
    });

    return success;
  }

  bool reprocessRecording({
    required String wavPath,
    required String targetModemId,
    String? jsonParams,
  }) {
    _activeStreamWavPath = wavPath;
    _activeStreamTxId = const Uuid().v4();
    _activeImageTxId = _activeStreamTxId;
    _activeStreamBuffer.clear();
    _hasEmittedTransmissionForCurrentBurst = false;

    final plugin = PluginRegistry.instance.get(targetModemId);
    if (plugin?.category == PayloadCategory.image) {
      RasterBridge.instance.clearCanvas();
      liveCanvasNotifier.value = null;
    }

    return bindings.reprocessRecording(
      wavPath: wavPath,
      targetModemId: targetModemId,
      jsonParams: jsonParams,
    );
  }

  Future<bool> importAudioRecording({
    required String sourceFilePath,
    String? targetModemId,
  }) async {
    final file = File(sourceFilePath);
    if (!file.existsSync()) return false;

    final recordingsDir = Directory(_storageDirectory);
    if (!recordingsDir.existsSync()) {
      recordingsDir.createSync(recursive: true);
    }

    final ext = p.extension(sourceFilePath).isEmpty ? '.wav' : p.extension(sourceFilePath);
    final fileName = 'imported_${DateTime.now().millisecondsSinceEpoch}$ext';
    final destinationPath = p.join(recordingsDir.path, fileName);

    await file.copy(destinationPath);

    final targetId = targetModemId ?? 'auto';
    final plugin = (targetId != 'auto' && targetId != 'sentry')
        ? PluginRegistry.instance.get(targetId)
        : null;

    final initialTx = Transmission(
      id: const Uuid().v4(),
      timestamp: DateTime.now().millisecondsSinceEpoch,
      direction: TransmissionDirection.rx,
      protocolId: plugin?.id ?? 'imported_audio',
      protocolDisplayName: plugin?.displayName ?? 'Imported Audio Burst',
      payloadType: plugin?.category.toPayloadType() ?? PayloadType.text,
      textContent: '[Imported Audio: ${p.basename(sourceFilePath)}]',
      snrDb: 20.0,
      durationMs: 3000,
      audioFilePath: destinationPath,
      isIdentified: plugin != null,
    );
    database.insertTransmission(initialTx);

    return reprocessRecording(
      wavPath: destinationPath,
      targetModemId: targetId,
    );
  }

  void updateSettings(StationSettings newSettings) {
    settingsNotifier.value = newSettings;
    try {
      database.saveStationSettings(newSettings);
    } catch (_) {}
    if (_bindings != null) {
      _bindings!.setLoopbackMode(newSettings.isLoopbackEnabled);
      _bindings!.setSquelchThreshold(newSettings.squelchThresholdDb);

      try {
        _bindings!.configureModem(
          'rattlegram',
          jsonEncode({
            'carrier_freq': newSettings.rattlegramCarrierFreq,
            'sensitivity': newSettings.rattlegramSensitivity,
            'mode': newSettings.rattlegramMode,
          }),
        );
        _bindings!.configureModem(
          'feld_hell',
          jsonEncode({
            'carrier_freq': newSettings.feldHellCarrierFreq,
            'mode': newSettings.feldHellMode,
          }),
        );
        _bindings!.configureModem(
          'eas_same',
          jsonEncode({
            'originator': newSettings.easOriginator,
            'event_code': newSettings.easEventCode,
          }),
        );
        _bindings!.configureModem(
          'cw_morse',
          jsonEncode({
            'pitch': newSettings.cwPitch,
            'wpm': newSettings.cwWpm,
          }),
        );
      } catch (_) {}
    }
  }

  void getFFTMagnitudes(Float32List buffer) {
    if (_bindings != null) {
      _bindings!.getFFTMagnitudes(buffer);
    }
  }

  void dispose() {
    _eventSub?.cancel();
    _eventPort.close();
    _bindings?.stopAudioHAL();
    database.close();
    _isInitialized = false;
  }
}
