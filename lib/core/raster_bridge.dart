import 'dart:ffi';
import 'dart:typed_data';
import 'dart:ui' as ui;
import 'ffi_bindings.dart';

class RasterBridge {
  static final RasterBridge instance = RasterBridge._internal();
  RasterBridge._internal();

  late final Pointer<Uint8> _nativePtr;
  late final Uint8List canvasView;
  bool _isInitialized = false;

  bool get isInitialized => _isInitialized;
  int get width => 640;
  int get height => 496;

  void initialize() {
    if (_isInitialized) return;
    final bindings = PolyglotNativeBindings.instance;
    _nativePtr = bindings.getSharedCanvasPtr();
    // Zero-copy: Direct memory projection over C++ heap (640 x 496 x 4 RGBA32)
    canvasView = _nativePtr.asTypedList(640 * 496 * 4);
    _isInitialized = true;
  }

  void clearCanvas({int argb = 0xFF000000}) {
    if (!_isInitialized) initialize();
    PolyglotNativeBindings.instance.clearCanvas(argb);
  }

  void decodeCurrentCanvas({
    int width = 640,
    int height = 496,
    required void Function(ui.Image) onFrameReady,
  }) {
    if (!_isInitialized) initialize();
    ui.decodeImageFromPixels(
      canvasView,
      width,
      height,
      ui.PixelFormat.rgba8888,
      onFrameReady,
    );
  }
}
