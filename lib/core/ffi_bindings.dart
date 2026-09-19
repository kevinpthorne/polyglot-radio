import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';
import 'package:ffi/ffi.dart';

// EventType matching C++
enum EventType {
  carrierDetected(1),
  carrierLost(2),
  packetDecoded(3),
  textStream(4),
  rasterLineReady(5),
  statusUpdate(6);

  final int value;
  const EventType(this.value);

  static EventType fromInt(int v) {
    return EventType.values.firstWhere(
      (e) => e.value == v,
      orElse: () => EventType.statusUpdate,
    );
  }
}

// Packed struct matching C++ struct NativeModemEvent
final class NativeModemEventStruct extends Struct {
  @Int32()
  external int eventType;

  external Pointer<Utf8> protocolId;

  @Float()
  external double snrDb;

  @Int32()
  external int centerFreq;

  external Pointer<Uint8> payload;

  @Size()
  external int payloadLen;

  @Int32()
  external int metadataInt;
}

typedef NativeEventCallback = Void Function(Pointer<NativeModemEventStruct>);

class PolyglotNativeBindings {
  static PolyglotNativeBindings? _instance;
  static PolyglotNativeBindings get instance => _instance ??= PolyglotNativeBindings._internal();

  late final DynamicLibrary lib;

  // Native function pointers
  late final int Function(Pointer<Void>) _initDartApiDL;
  late final void Function(int) _registerEventPort;
  late final void Function(Pointer<NativeFunction<NativeEventCallback>>) _registerEventCallback;

  late final bool Function(int, bool) _startAudioHAL;
  late final void Function() _stopAudioHAL;
  late final bool Function() _isAudioHALRunning;
  late final void Function(bool) _setLoopbackMode;
  late final void Function(double) _setSquelchThreshold;
  late final double Function() _getSquelchLevel;
  late final bool Function() _isSquelchOpen;

  late final bool Function(Pointer<Utf8>, Pointer<Uint8>, int, Pointer<Utf8>) _startTransmit;
  late final void Function(Pointer<Utf8>, Pointer<Utf8>) _configureModem;
  late final bool Function() _isTransmitting;
  late final void Function() _abortTransmit;

  late final Pointer<Uint8> Function() _getSharedCanvasPtr;
  late final int Function() _getSharedCanvasSize;
  late final void Function(int) _clearCanvas;

  late final void Function(Pointer<Float>, int) _getFFTMagnitudes;
  late final bool Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>) _reprocessRecording;
  late final void Function(Pointer<Utf8>) _setStorageDirectory;
  late final void Function(Pointer<Float>, int) _injectAudioSamples;

  late final void Function(bool) _setOutputMuted;
  late final bool Function() _isOutputMuted;
  late final void Function(bool) _setInputMuted;
  late final bool Function() _isInputMuted;

  late final bool Function(Pointer<Utf8>) _playAudioFile;
  late final void Function() _pauseAudioPlayback;
  late final void Function() _resumeAudioPlayback;
  late final void Function() _stopAudioPlayback;
  late final bool Function() _isAudioPlaying;
  late final double Function() _getAudioPlaybackPosition;
  late final double Function() _getAudioPlaybackDuration;
  late final void Function(double) _seekAudioPlayback;

  PolyglotNativeBindings._internal() {
    lib = _loadLibrary();

    _initDartApiDL = lib
        .lookup<NativeFunction<IntPtr Function(Pointer<Void>)>>('Native_InitDartApiDL')
        .asFunction();

    _registerEventPort = lib
        .lookup<NativeFunction<Void Function(Int64)>>('Native_RegisterEventPort')
        .asFunction();

    _registerEventCallback = lib
        .lookup<NativeFunction<Void Function(Pointer<NativeFunction<NativeEventCallback>>)>>('Native_RegisterEventCallback')
        .asFunction();

    _startAudioHAL = lib
        .lookup<NativeFunction<Bool Function(Int32, Bool)>>('Native_StartAudioHAL')
        .asFunction();

    _stopAudioHAL = lib
        .lookup<NativeFunction<Void Function()>>('Native_StopAudioHAL')
        .asFunction();

    _isAudioHALRunning = lib
        .lookup<NativeFunction<Bool Function()>>('Native_IsAudioHALRunning')
        .asFunction();

    _setLoopbackMode = lib
        .lookup<NativeFunction<Void Function(Bool)>>('Native_SetLoopbackMode')
        .asFunction();

    _setSquelchThreshold = lib
        .lookup<NativeFunction<Void Function(Float)>>('Native_SetSquelchThreshold')
        .asFunction();

    _getSquelchLevel = lib
        .lookup<NativeFunction<Float Function()>>('Native_GetSquelchLevel')
        .asFunction();

    _isSquelchOpen = lib
        .lookup<NativeFunction<Bool Function()>>('Native_IsSquelchOpen')
        .asFunction();

    _startTransmit = lib
        .lookup<NativeFunction<Bool Function(Pointer<Utf8>, Pointer<Uint8>, Size, Pointer<Utf8>)>>('Native_StartTransmit')
        .asFunction();

    _configureModem = lib
        .lookup<NativeFunction<Void Function(Pointer<Utf8>, Pointer<Utf8>)>>('Native_ConfigureModem')
        .asFunction();

    _isTransmitting = lib
        .lookup<NativeFunction<Bool Function()>>('Native_IsTransmitting')
        .asFunction();

    _abortTransmit = lib
        .lookup<NativeFunction<Void Function()>>('Native_AbortTransmit')
        .asFunction();

    _getSharedCanvasPtr = lib
        .lookup<NativeFunction<Pointer<Uint8> Function()>>('Native_GetSharedCanvasPtr')
        .asFunction();

    _getSharedCanvasSize = lib
        .lookup<NativeFunction<Size Function()>>('Native_GetSharedCanvasSize')
        .asFunction();

    _clearCanvas = lib
        .lookup<NativeFunction<Void Function(Uint32)>>('Native_ClearCanvas')
        .asFunction();

    _getFFTMagnitudes = lib
        .lookup<NativeFunction<Void Function(Pointer<Float>, Size)>>('Native_GetFFTMagnitudes')
        .asFunction();

    _reprocessRecording = lib
        .lookup<NativeFunction<Bool Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>)>>('Native_ReprocessRecording')
        .asFunction();

    _setStorageDirectory = lib
        .lookup<NativeFunction<Void Function(Pointer<Utf8>)>>('Native_SetStorageDirectory')
        .asFunction();

    _injectAudioSamples = lib
        .lookup<NativeFunction<Void Function(Pointer<Float>, Size)>>('Native_InjectAudioSamples')
        .asFunction();

    _setOutputMuted = lib
        .lookup<NativeFunction<Void Function(Bool)>>('Native_SetOutputMuted')
        .asFunction();

    _isOutputMuted = lib
        .lookup<NativeFunction<Bool Function()>>('Native_IsOutputMuted')
        .asFunction();

    _setInputMuted = lib
        .lookup<NativeFunction<Void Function(Bool)>>('Native_SetInputMuted')
        .asFunction();

    _isInputMuted = lib
        .lookup<NativeFunction<Bool Function()>>('Native_IsInputMuted')
        .asFunction();

    _playAudioFile = lib
        .lookup<NativeFunction<Bool Function(Pointer<Utf8>)>>('Native_PlayAudioFile')
        .asFunction();

    _pauseAudioPlayback = lib
        .lookup<NativeFunction<Void Function()>>('Native_PauseAudioPlayback')
        .asFunction();

    _resumeAudioPlayback = lib
        .lookup<NativeFunction<Void Function()>>('Native_ResumeAudioPlayback')
        .asFunction();

    _stopAudioPlayback = lib
        .lookup<NativeFunction<Void Function()>>('Native_StopAudioPlayback')
        .asFunction();

    _isAudioPlaying = lib
        .lookup<NativeFunction<Bool Function()>>('Native_IsAudioPlaying')
        .asFunction();

    _getAudioPlaybackPosition = lib
        .lookup<NativeFunction<Float Function()>>('Native_GetAudioPlaybackPosition')
        .asFunction();

    _getAudioPlaybackDuration = lib
        .lookup<NativeFunction<Float Function()>>('Native_GetAudioPlaybackDuration')
        .asFunction();

    _seekAudioPlayback = lib
        .lookup<NativeFunction<Void Function(Float)>>('Native_SeekAudioPlayback')
        .asFunction();

    // Initialize Dart API DL
    _initDartApiDL(NativeApi.initializeApiDLData);
  }

  static DynamicLibrary _loadLibrary() {
    if (Platform.isMacOS) {
      final exe = File(Platform.resolvedExecutable);
      final exeDir = exe.parent;

      // 1. Check inside macOS app bundle Frameworks / MacOS
      final bundleCandidates = [
        '${exeDir.path}/../Frameworks/libpolyglot_native.dylib',
        '${exeDir.path}/libpolyglot_native.dylib',
      ];
      for (final p in bundleCandidates) {
        if (File(p).existsSync()) {
          try {
            return DynamicLibrary.open(p);
          } catch (_) {}
        }
      }

      // 2. Relative to current working directory (e.g. tests or CLI)
      final cwdCandidates = [
        '${Directory.current.path}/native_core/build/libpolyglot_native.dylib',
        'native_core/build/libpolyglot_native.dylib',
        '../native_core/build/libpolyglot_native.dylib',
        '../../native_core/build/libpolyglot_native.dylib',
      ];
      for (final p in cwdCandidates) {
        if (File(p).existsSync()) {
          try {
            return DynamicLibrary.open(p);
          } catch (_) {}
        }
      }

      // 3. Walk up the directory tree from the executable to find project root / native_core
      var dir = exeDir;
      for (int i = 0; i < 12; i++) {
        final checkPath = '${dir.path}/native_core/build/libpolyglot_native.dylib';
        if (File(checkPath).existsSync()) {
          try {
            return DynamicLibrary.open(checkPath);
          } catch (_) {}
        }
        final parent = dir.parent;
        if (parent.path == dir.path) break;
        dir = parent;
      }

      // 4. Default dyld search
      try {
        return DynamicLibrary.open('libpolyglot_native.dylib');
      } catch (_) {
        return DynamicLibrary.process();
      }
    } else if (Platform.isLinux) {
      final exe = File(Platform.resolvedExecutable);
      final exeDir = exe.parent;

      final bundleCandidates = [
        '${exeDir.path}/lib/libpolyglot_native.so',
        '${exeDir.path}/libpolyglot_native.so',
      ];
      for (final p in bundleCandidates) {
        if (File(p).existsSync()) {
          try {
            return DynamicLibrary.open(p);
          } catch (_) {}
        }
      }

      final cwdCandidates = [
        '${Directory.current.path}/native_core/build/libpolyglot_native.so',
        'native_core/build/libpolyglot_native.so',
        '../native_core/build/libpolyglot_native.so',
        '../../native_core/build/libpolyglot_native.so',
      ];
      for (final p in cwdCandidates) {
        if (File(p).existsSync()) {
          try {
            return DynamicLibrary.open(p);
          } catch (_) {}
        }
      }

      var dir = exeDir;
      for (int i = 0; i < 12; i++) {
        final checkPath = '${dir.path}/native_core/build/libpolyglot_native.so';
        if (File(checkPath).existsSync()) {
          try {
            return DynamicLibrary.open(checkPath);
          } catch (_) {}
        }
        final parent = dir.parent;
        if (parent.path == dir.path) break;
        dir = parent;
      }

      try {
        return DynamicLibrary.open('libpolyglot_native.so');
      } catch (_) {
        return DynamicLibrary.process();
      }
    } else if (Platform.isAndroid) {
      return DynamicLibrary.open('libpolyglot_native.so');
    } else if (Platform.isIOS) {
      final candidates = [
        'polyglot_native.framework/polyglot_native',
        'Frameworks/polyglot_native.framework/polyglot_native',
      ];
      for (final p in candidates) {
        try {
          return DynamicLibrary.open(p);
        } catch (_) {}
      }
      return DynamicLibrary.process();
    }
    throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
  }

  // High-Level Dart Wrappers
  void registerEventPort(int portId) => _registerEventPort(portId);

  void registerEventCallback(Pointer<NativeFunction<NativeEventCallback>> cb) =>
      _registerEventCallback(cb);

  bool startAudioHAL({int sampleRate = 48000, bool enableLoopback = false}) =>
      _startAudioHAL(sampleRate, enableLoopback);

  void stopAudioHAL() => _stopAudioHAL();
  bool isAudioHALRunning() => _isAudioHALRunning();
  void setLoopbackMode(bool enabled) => _setLoopbackMode(enabled);
  void setSquelchThreshold(double db) => _setSquelchThreshold(db);
  double getSquelchLevel() => _getSquelchLevel();
  bool isSquelchOpen() => _isSquelchOpen();

  bool startTransmit({
    required String protocolId,
    required Uint8List payload,
    String? jsonConfig,
  }) {
    final protoPtr = protocolId.toNativeUtf8();
    final configPtr = (jsonConfig ?? '{}').toNativeUtf8();
    final payloadPtr = calloc<Uint8>(payload.length);
    final payloadView = payloadPtr.asTypedList(payload.length);
    payloadView.setAll(0, payload);

    try {
      return _startTransmit(protoPtr, payloadPtr, payload.length, configPtr);
    } finally {
      calloc.free(protoPtr);
      calloc.free(configPtr);
      calloc.free(payloadPtr);
    }
  }

  bool isTransmitting() => _isTransmitting();
  void abortTransmit() => _abortTransmit();

  void configureModem(String modemId, String jsonConfig) {
    final modemPtr = modemId.toNativeUtf8();
    final configPtr = jsonConfig.toNativeUtf8();
    try {
      _configureModem(modemPtr, configPtr);
    } finally {
      calloc.free(modemPtr);
      calloc.free(configPtr);
    }
  }

  Pointer<Uint8> getSharedCanvasPtr() => _getSharedCanvasPtr();
  int getSharedCanvasSize() => _getSharedCanvasSize();
  void clearCanvas(int argb) => _clearCanvas(argb);

  void getFFTMagnitudes(Float32List outMags) {
    final ptr = calloc<Float>(outMags.length);
    try {
      _getFFTMagnitudes(ptr, outMags.length);
      final view = ptr.asTypedList(outMags.length);
      outMags.setAll(0, view);
    } finally {
      calloc.free(ptr);
    }
  }

  bool reprocessRecording({
    required String wavPath,
    required String targetModemId,
    String? jsonParams,
  }) {
    final pathPtr = wavPath.toNativeUtf8();
    final modemPtr = targetModemId.toNativeUtf8();
    final jsonPtr = (jsonParams ?? '{}').toNativeUtf8();
    try {
      return _reprocessRecording(pathPtr, modemPtr, jsonPtr);
    } finally {
      calloc.free(pathPtr);
      calloc.free(modemPtr);
      calloc.free(jsonPtr);
    }
  }

  void setStorageDirectory(String dirPath) {
    final dirPtr = dirPath.toNativeUtf8();
    try {
      _setStorageDirectory(dirPtr);
    } finally {
      calloc.free(dirPtr);
    }
  }

  void injectAudioSamples(Float32List samples) {
    final ptr = calloc<Float>(samples.length);
    final view = ptr.asTypedList(samples.length);
    view.setAll(0, samples);
    try {
      _injectAudioSamples(ptr, samples.length);
    } finally {
      calloc.free(ptr);
    }
  }

  void setOutputMuted(bool muted) => _setOutputMuted(muted);
  bool isOutputMuted() => _isOutputMuted();
  void setInputMuted(bool muted) => _setInputMuted(muted);
  bool isInputMuted() => _isInputMuted();

  bool playAudioFile(String path) {
    final ptr = path.toNativeUtf8();
    try {
      return _playAudioFile(ptr);
    } finally {
      calloc.free(ptr);
    }
  }

  void pauseAudioPlayback() => _pauseAudioPlayback();
  void resumeAudioPlayback() => _resumeAudioPlayback();
  void stopAudioPlayback() => _stopAudioPlayback();
  bool isAudioPlaying() => _isAudioPlaying();
  double getAudioPlaybackPosition() => _getAudioPlaybackPosition();
  double getAudioPlaybackDuration() => _getAudioPlaybackDuration();
  void seekAudioPlayback(double seconds) => _seekAudioPlayback(seconds);
}
