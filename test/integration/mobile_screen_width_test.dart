import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqlite3/sqlite3.dart';
import 'package:polyglot_radio/core/modem_coordinator.dart';
import 'package:polyglot_radio/database/app_database.dart';
import 'package:polyglot_radio/database/models.dart';
import 'package:polyglot_radio/ui/home_screen.dart';
import 'package:polyglot_radio/ui/widgets/transmission_bubble.dart';

void main() {
  setUpAll(() async {
    final db = AppDatabase(sqlite3.openInMemory());
    await ModemCoordinator.instance.initialize(
      customDb: db,
      enableLoopback: true,
    );
  });

  testWidgets('HomeScreen renders cleanly on iPhone SE (375x667) without overflow', (WidgetTester tester) async {
    tester.view.physicalSize = const Size(375, 667);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() {
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });

    await tester.pumpWidget(
      const MaterialApp(
        home: HomeScreen(),
      ),
    );
    await tester.pumpAndSettle();

    // Verify key elements are rendered
    expect(find.text('Polyglot Radio: Acoustic SDR'), findsOneWidget);
    expect(find.byKey(const Key('tx_mute_button')), findsOneWidget);
    expect(find.byKey(const Key('rx_mute_button')), findsOneWidget);
    expect(find.byIcon(Icons.settings_outlined), findsOneWidget);
    expect(find.byIcon(Icons.file_upload_outlined), findsOneWidget);

    // No overflow errors recorded
    expect(tester.takeException(), isNull);
  });

  testWidgets('HomeScreen renders cleanly on 360px mobile width without overflow', (WidgetTester tester) async {
    tester.view.physicalSize = const Size(360, 640);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() {
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });

    await tester.pumpWidget(
      const MaterialApp(
        home: HomeScreen(),
      ),
    );
    await tester.pumpAndSettle();

    // Verify elements rendered
    expect(find.text('Polyglot Radio: Acoustic SDR'), findsOneWidget);
    expect(find.byKey(const Key('tx_mute_button')), findsOneWidget);
    expect(find.byKey(const Key('rx_mute_button')), findsOneWidget);

    // No overflow errors recorded
    expect(tester.takeException(), isNull);
  });

  testWidgets('TransmissionBubble with long protocol name and SNR fits on iPhone SE width', (WidgetTester tester) async {
    tester.view.physicalSize = const Size(375, 667);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() {
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });

    final tx = Transmission(
      id: 'tx-1',
      direction: TransmissionDirection.rx,
      protocolId: 'hellschreiber_feld',
      protocolDisplayName: 'Hellschreiber (Feld)',
      payloadType: PayloadType.text,
      textContent: 'HELLO WORLD DE K6OTA',
      timestamp: DateTime.now().millisecondsSinceEpoch,
      durationMs: 1500,
      snrDb: 18.5,
      isIdentified: true,
      audioFilePath: '/tmp/test.wav',
    );

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: TransmissionBubble(transmission: tx),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('HELLSCHREIBER (FELD)'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
