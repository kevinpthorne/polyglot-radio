import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:polyglot_radio/core/ffi_bindings.dart';
import 'package:polyglot_radio/core/raster_bridge.dart';

void main() {
  group('Native C++ FFI Bridge Integration Tests', () {
    late PolyglotNativeBindings bindings;

    setUpAll(() {
      bindings = PolyglotNativeBindings.instance;
    });

    test('Native library successfully loaded and export pointers resolved', () {
      expect(bindings, isNotNull);
      expect(bindings.lib, isNotNull);
    });

    test('Audio HAL start, loopback configuration, and squelch controls', () {
      final started = bindings.startAudioHAL(
        sampleRate: 48000,
        enableLoopback: true,
      );
      expect(started, isTrue);
      expect(bindings.isAudioHALRunning(), isTrue);

      bindings.setLoopbackMode(true);
      bindings.setSquelchThreshold(-75.0);

      final squelchLevel = bindings.getSquelchLevel();
      expect(squelchLevel, isA<double>());

      final isOpen = bindings.isSquelchOpen();
      expect(isOpen, isA<bool>());
    });

    test('FFT 512-bin magnitude projection into Dart buffer', () {
      final buffer = Float32List(512);
      bindings.getFFTMagnitudes(buffer);

      // Verify buffer has 512 elements and non-NaN values
      expect(buffer.length, equals(512));
      for (int i = 0; i < buffer.length; i++) {
        expect(buffer[i].isNaN, isFalse);
        expect(buffer[i].isInfinite, isFalse);
      }
    });

    test('Shared 640x496 RGBA canvas memory mapping and RasterBridge', () {
      final canvasSize = bindings.getSharedCanvasSize();
      const expectedSize = 640 * 496 * 4; // 1,269,760 bytes
      expect(canvasSize, equals(expectedSize));

      final canvasPtr = bindings.getSharedCanvasPtr();
      expect(canvasPtr.address, isNonZero);

      // Clear canvas to solid opaque black
      bindings.clearCanvas(0xFF000000);

      // Test RasterBridge initialization
      RasterBridge.instance.initialize();
      expect(RasterBridge.instance.isInitialized, isTrue);
      expect(RasterBridge.instance.width, equals(640));
      expect(RasterBridge.instance.height, equals(496));
    });

    test('Audio HAL gracefully stops', () {
      bindings.stopAudioHAL();
      expect(bindings.isAudioHALRunning(), isFalse);
    });
  });
}
