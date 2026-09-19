import 'dart:async';
import 'dart:typed_data';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import '../../core/modem_coordinator.dart';

class WaterfallView extends StatefulWidget {
  final double height;
  const WaterfallView({super.key, this.height = 180.0});

  @override
  State<WaterfallView> createState() => _WaterfallViewState();
}

class _WaterfallViewState extends State<WaterfallView> {
  final Float32List _fftMagnitudes = Float32List(512);
  Timer? _refreshTimer;
  ui.Image? _historyTexture;

  @override
  void initState() {
    super.initState();
    // Poll native FFT at 30 FPS (~33ms)
    _refreshTimer = Timer.periodic(const Duration(milliseconds: 33), (timer) {
      if (mounted) {
        ModemCoordinator.instance.getFFTMagnitudes(_fftMagnitudes);
        setState(() {});
      }
    });
  }

  @override
  void dispose() {
    _refreshTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      height: widget.height,
      decoration: BoxDecoration(
        color: const Color(0xFF0D1117),
        border: Border(
          bottom: BorderSide(color: Colors.cyanAccent.withOpacity(0.3), width: 1.0),
        ),
      ),
      child: Stack(
        children: [
          CustomPaint(
            size: Size(double.infinity, widget.height),
            painter: WaterfallPainter(
              fftMagnitudes: _fftMagnitudes,
              historyTexture: _historyTexture,
            ),
          ),
          // Frequency grid overlay
          Positioned(
            bottom: 4,
            left: 8,
            right: 8,
            child: LayoutBuilder(
              builder: (context, constraints) {
                final isNarrow = constraints.maxWidth < 420;
                return Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    const Text("0 Hz", style: TextStyle(color: Colors.white54, fontSize: 9.5, fontFamily: 'monospace')),
                    Text(isNarrow ? "6k" : "6 kHz", style: const TextStyle(color: Colors.white54, fontSize: 9.5, fontFamily: 'monospace')),
                    Text(isNarrow ? "12k" : "12 kHz", style: const TextStyle(color: Colors.white54, fontSize: 9.5, fontFamily: 'monospace')),
                    Text(isNarrow ? "18k" : "18 kHz", style: const TextStyle(color: Colors.cyanAccent, fontSize: 9.5, fontFamily: 'monospace')),
                    Text(isNarrow ? "24k" : "24 kHz", style: const TextStyle(color: Colors.white54, fontSize: 9.5, fontFamily: 'monospace')),
                  ],
                );
              },
            ),
          ),
        ],
      ),
    );
  }
}

class WaterfallPainter extends CustomPainter {
  final Float32List fftMagnitudes; // 512 bins, 0 to 24 kHz
  final ui.Image? historyTexture;

  WaterfallPainter({required this.fftMagnitudes, this.historyTexture});

  @override
  void paint(Canvas canvas, Size size) {
    // 1. Shift historic spectrogram if texture available
    if (historyTexture != null) {
      canvas.drawImageRect(
        historyTexture!,
        Rect.fromLTWH(0, 0, historyTexture!.width.toDouble(), historyTexture!.height.toDouble() - 1),
        Rect.fromLTWH(0, 1, size.width, size.height),
        Paint(),
      );
    }

    // 2. Paint current FFT slice at top (y = 0) and live frequency spectrum bars
    final paint = Paint()
      ..style = PaintingStyle.fill
      ..strokeWidth = 1.0;

    final binWidth = size.width / fftMagnitudes.length;

    for (int i = 0; i < fftMagnitudes.length; ++i) {
      final mag = fftMagnitudes[i].clamp(0.0, 1.0);
      paint.color = _mapThermalColor(mag);

      // Spectrogram line at top
      canvas.drawRect(
        Rect.fromLTWH(i * binWidth, 0, binWidth + 0.5, 4),
        paint,
      );

      // Real-time spectrum line
      final barHeight = mag * (size.height - 24);
      paint.color = paint.color.withOpacity(0.7);
      canvas.drawRect(
        Rect.fromLTWH(i * binWidth, size.height - 20 - barHeight, binWidth + 0.5, barHeight),
        paint,
      );
    }
  }

  Color _mapThermalColor(double normalizedVal) {
    // Thermal spectrum colormap: Black -> Blue -> Purple -> Red -> Yellow -> White
    if (normalizedVal < 0.05) return const Color(0xFF000511);
    return HSVColor.fromAHSV(1.0, (1.0 - normalizedVal) * 240.0, 1.0, normalizedVal).toColor();
  }

  @override
  bool shouldRepaint(covariant WaterfallPainter oldDelegate) => true;
}
