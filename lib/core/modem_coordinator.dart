import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';
import 'package:path/path.dart' as p;
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

  late final PolyglotNativeBindings bindings;
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

  // Buffer for active text streams (e.g. CW / PSK31 tokens)
  String? _activeStreamTxId;
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
    RasterBridge.instance.initialize();
    bindings = PolyglotNativeBindings.instance;

    database = customDb ?? await AppDatabase.openDefault();

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
    bindings.setStorageDirectory(audioDir);

    // Register event port for asynchronous C++ dispatch
    bindings.registerEventPort(_eventPort.sendPort.nativePort);
    _eventSub = _eventPort.listen(_handleNativeEvent);

    // Start native audio hardware / loopback HAL
    bindings.startAudioHAL(
      sampleRate: 48000,
      enableLoopback: enableLoopback,
    );

    settingsNotifier.value = settingsNotifier.value.copyWith(
      isLoopbackEnabled: enableLoopback,
    );

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
        _activeStreamBuffer.clear();
        break;

      case EventType.carrierLost:
        isSentryLockedNotifier.value = false;
        activeProtocolNotifier.value = null;

        // If we were accumulating a continuous text stream (CW / PSK31 / Hell)
        if (_activeStreamTxId != null && _activeStreamBuffer.isNotEmpty) {
          final text = _activeStreamBuffer.toString();
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
          _activeStreamTxId = null;
          _activeStreamBuffer.clear();
        }
        break;

      case EventType.textStream:
        final charStr = utf8.decode(payload, allowMalformed: true);
        _activeStreamBuffer.write(charStr);
        break;

      case EventType.packetDecoded:
        final text = utf8.decode(payload, allowMalformed: true);
        final tx = Transmission(
          id: const Uuid().v4(),
          timestamp: DateTime.now().millisecondsSinceEpoch,
          direction: TransmissionDirection.rx,
          protocolId: protocolId,
          protocolDisplayName: displayName,
          payloadType: plugin?.category.toPayloadType() ?? PayloadType.packet,
          textContent: text,
          rawPayload: payload,
          snrDb: snrDb,
          durationMs: metadataInt > 0 ? metadataInt : 1000,
          audioFilePath: _activeStreamWavPath,
          isIdentified: true,
        );
        database.insertTransmission(tx);
        break;

      case EventType.rasterLineReady:
        // Raster line received on shared canvas
        break;

      case EventType.statusUpdate:
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

    final payload = rawBytes ?? Uint8List.fromList(utf8.encode(text));
    final configMap = plugin.getDefaultConfig();
    if (settingsNotifier.value.callsign != null) {
      configMap['callsign'] = settingsNotifier.value.callsign!;
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

    // Monitor transmission completion
    Timer.periodic(const Duration(milliseconds: 100), (timer) {
      if (!bindings.isTransmitting()) {
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
    bindings.setLoopbackMode(newSettings.isLoopbackEnabled);
    bindings.setSquelchThreshold(newSettings.squelchThresholdDb);
  }

  void getFFTMagnitudes(Float32List buffer) {
    bindings.getFFTMagnitudes(buffer);
  }

  void dispose() {
    _eventSub?.cancel();
    _eventPort.close();
    bindings.stopAudioHAL();
    database.close();
    _isInitialized = false;
  }
}
