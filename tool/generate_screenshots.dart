import 'dart:io';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:sqlite3/sqlite3.dart';

import 'package:polyglot_radio/core/modem_coordinator.dart';
import 'package:polyglot_radio/database/app_database.dart';
import 'package:polyglot_radio/database/models.dart';
import 'package:polyglot_radio/ui/home_screen.dart';
import 'package:polyglot_radio/ui/settings/station_settings_dialog.dart';
import 'package:polyglot_radio/ui/widgets/audio_import_dialog.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // 1. Initialize coordinator with in-memory database & loopback
  final db = AppDatabase(sqlite3.openInMemory());
  final coordinator = ModemCoordinator.instance;
  await coordinator.initialize(customDb: db, enableLoopback: true);

  // 2. Insert mock sample transmissions for rich marketing visuals
  db.insertTransmission(
    Transmission(
      id: 'tx-aprs-1',
      direction: TransmissionDirection.rx,
      protocolId: 'aprs_bell202',
      protocolDisplayName: 'APRS (Bell 202)',
      payloadType: PayloadType.text,
      textContent: 'K6OTA-9>APRS,WIDE1-1:!3745.12N/12226.88W#Acoustic Tracker',
      timestamp: DateTime.now().subtract(const Duration(minutes: 3)).millisecondsSinceEpoch,
      durationMs: 1200,
      audioFilePath: '/tmp/test_aprs.wav',
      snrDb: 19.4,
      isIdentified: true,
    ),
  );

  db.insertTransmission(
    Transmission(
      id: 'tx-sstv-1',
      direction: TransmissionDirection.rx,
      protocolId: 'sstv_robot36',
      protocolDisplayName: 'SSTV (Robot 36)',
      payloadType: PayloadType.image,
      textContent: 'VIS 0x88 Sync - Robot 36 Scan Complete (320x240)',
      timestamp: DateTime.now().subtract(const Duration(minutes: 1)).millisecondsSinceEpoch,
      durationMs: 36000,
      audioFilePath: '/tmp/test_sstv.wav',
      snrDb: 24.8,
      isIdentified: true,
    ),
  );

  db.insertTransmission(
    Transmission(
      id: 'tx-ultra-1',
      direction: TransmissionDirection.tx,
      protocolId: 'ultrasound',
      protocolDisplayName: 'Ultrasound (19 kHz)',
      payloadType: PayloadType.text,
      textContent: 'POLYGLOT_ACOUSTIC_MESH_BEACON_RS9',
      timestamp: DateTime.now().millisecondsSinceEpoch,
      durationMs: 850,
      audioFilePath: '/tmp/test_ultra.wav',
      snrDb: 28.5,
      isIdentified: true,
    ),
  );

  db.insertTransmission(
    Transmission(
      id: 'tx-cw-1',
      direction: TransmissionDirection.rx,
      protocolId: 'cw_morse',
      protocolDisplayName: 'CW Morse Code',
      payloadType: PayloadType.text,
      textContent: 'CQ CQ CQ DE W6XYZ K',
      timestamp: DateTime.now().add(const Duration(seconds: 10)).millisecondsSinceEpoch,
      durationMs: 4200,
      audioFilePath: '/tmp/test_cw.wav',
      snrDb: 15.1,
      isIdentified: true,
    ),
  );

  runApp(const ScreenshotGeneratorApp());
}

class ScreenshotGeneratorApp extends StatefulWidget {
  const ScreenshotGeneratorApp({super.key});

  @override
  State<ScreenshotGeneratorApp> createState() => _ScreenshotGeneratorAppState();
}

class _ScreenshotGeneratorAppState extends State<ScreenshotGeneratorApp> {
  final GlobalKey _repaintKey = GlobalKey();
  String _status = 'Starting screenshot captures...';

  final List<_ScreenshotTarget> _targets = [
    // iOS: 1284 x 2778 px (iPhone 6.7" Super Retina)
    _ScreenshotTarget(
      fileName: 'ios_01_waterfall_timeline.png',
      width: 428,
      height: 926,
      pixelRatio: 3.0,
      widget: const HomeScreen(),
    ),
    _ScreenshotTarget(
      fileName: 'ios_02_station_settings.png',
      width: 428,
      height: 926,
      pixelRatio: 3.0,
      widget: const Scaffold(
        backgroundColor: Color(0xFF0D1117),
        body: Center(
          child: SingleChildScrollView(child: StationSettingsDialog()),
        ),
      ),
    ),
    _ScreenshotTarget(
      fileName: 'ios_03_audio_import.png',
      width: 428,
      height: 926,
      pixelRatio: 3.0,
      widget: const Scaffold(
        backgroundColor: Color(0xFF0D1117),
        body: Center(
          child: SingleChildScrollView(child: AudioImportDialog()),
        ),
      ),
    ),
    // macOS: 2560 x 1600 px (16:10 Retina)
    _ScreenshotTarget(
      fileName: 'macos_01_desktop_main.png',
      width: 1280,
      height: 800,
      pixelRatio: 2.0,
      widget: const HomeScreen(),
    ),
    _ScreenshotTarget(
      fileName: 'macos_02_desktop_settings.png',
      width: 1280,
      height: 800,
      pixelRatio: 2.0,
      widget: const Scaffold(
        backgroundColor: Color(0xFF0D1117),
        body: Center(
          child: SizedBox(width: 600, child: StationSettingsDialog()),
        ),
      ),
    ),
  ];

  int _currentIndex = 0;

  @override
  void initState() {
    super.initState();
    _processNext();
  }

  Future<void> _processNext() async {
    if (_currentIndex >= _targets.length) {
      setState(() {
        _status = 'All screenshots captured successfully!';
      });
      print('=== DONE: All screenshots saved to build/store_screenshots/ ===');
      exit(0);
    }

    final target = _targets[_currentIndex];
    setState(() {
      _status = 'Rendering ${target.fileName}...';
    });

    // Wait 250ms for layout and widget tree to settle
    await Future.delayed(const Duration(milliseconds: 250));

    try {
      final boundary = _repaintKey.currentContext?.findRenderObject() as RenderRepaintBoundary?;
      if (boundary != null) {
        final image = await boundary.toImage(pixelRatio: target.pixelRatio);
        final byteData = await image.toByteData(format: ui.ImageByteFormat.png);
        image.dispose();

        if (byteData != null) {
          final dir = Directory('build/store_screenshots');
          if (!dir.existsSync()) dir.createSync(recursive: true);

          final file = File('${dir.path}/${target.fileName}');
          await file.writeAsBytes(byteData.buffer.asUint8List());
          print('Saved: ${file.path} (${image.width}x${image.height} px)');
        }
      }
    } catch (e) {
      print('Error capturing ${target.fileName}: $e');
    }

    _currentIndex++;
    if (mounted) {
      setState(() {});
      _processNext();
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_currentIndex >= _targets.length) {
      return MaterialApp(
        home: Scaffold(
          backgroundColor: const Color(0xFF0D1117),
          body: Center(
            child: Text(
              _status,
              style: const TextStyle(color: Colors.greenAccent, fontSize: 18),
            ),
          ),
        ),
      );
    }

    final target = _targets[_currentIndex];

    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: Scaffold(
        backgroundColor: const Color(0xFF0D1117),
        body: Center(
          child: FittedBox(
            child: SizedBox(
              width: target.width,
              height: target.height,
              child: RepaintBoundary(
                key: _repaintKey,
                child: MaterialApp(
                  debugShowCheckedModeBanner: false,
                  theme: ThemeData.dark(),
                  home: target.widget,
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _ScreenshotTarget {
  final String fileName;
  final double width;
  final double height;
  final double pixelRatio;
  final Widget widget;

  _ScreenshotTarget({
    required this.fileName,
    required this.width,
    required this.height,
    required this.pixelRatio,
    required this.widget,
  });
}
