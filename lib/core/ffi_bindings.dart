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
  late final bool Function() _isTransmitting;
  late final void Function() _abortTransmit;

  late final Pointer<Uint8> Function() _getSharedCanvasPtr;
  late final int Function() _getSharedCanvasSize;
  late final void Function(int) _clearCanvas;

  late final void Function(Pointer<Float>, int) _getFFTMagnitudes;
  late final bool Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>) _reprocessRecording;
  late final void Function(Pointer<Utf8>) _setStorageDirectory;
  late final void Function(Pointer<Float>, int) _injectAudioSamples;

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

    // Initialize Dart API DL
    _initDartApiDL(NativeApi.initializeApiDLData);
  }

  static DynamicLibrary _loadLibrary() {
    if (Platform.isMacOS) {
      // Check multiple standard locations
      final candidates = [
        'native_core/build/libpolyglot_native.dylib',
        '../native_core/build/libpolyglot_native.dylib',
        '../../native_core/build/libpolyglot_native.dylib',
        '${Directory.current.path}/native_core/build/libpolyglot_native.dylib',
        'libpolyglot_native.dylib',
      ];
      for (final path in candidates) {
        if (File(path).existsSync()) {
          return DynamicLibrary.open(path);
        }
      }
      return DynamicLibrary.process();
    } else if (Platform.isLinux) {
      final candidates = [
        'native_core/build/libpolyglot_native.so',
        'libpolyglot_native.so',
      ];
      for (final path in candidates) {
        if (File(path).existsSync()) {
          return DynamicLibrary.open(path);
        }
      }
      return DynamicLibrary.process();
    } else if (Platform.isAndroid) {
      return DynamicLibrary.open('libpolyglot_native.so');
    } else if (Platform.isIOS) {
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
}
