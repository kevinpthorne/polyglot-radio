import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqlite3/sqlite3.dart';
import 'package:polyglot_radio/core/modem_coordinator.dart';
import 'package:polyglot_radio/database/app_database.dart';
import 'package:polyglot_radio/ui/home_screen.dart';
import 'package:polyglot_radio/ui/widgets/status_banner.dart';
import 'package:polyglot_radio/ui/composer/transmission_composer.dart';

void main() {
  setUpAll(() async {
    final db = AppDatabase(sqlite3.openInMemory());
    await ModemCoordinator.instance.initialize(
      customDb: db,
      enableLoopback: true,
    );
  });

  testWidgets('HomeScreen renders title, StatusBanner, Waterfall, and Composer', (WidgetTester tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: HomeScreen(),
      ),
    );

    // Verify Title
    expect(find.text('Polyglot Radio: Acoustic SDR'), findsOneWidget);
    expect(find.text('ACOUSTIC SOFTWARE-DEFINED MODEM'), findsOneWidget);

    // Verify Widgets Present
    expect(find.byType(StatusBanner), findsOneWidget);
    expect(find.byType(TransmissionComposer), findsOneWidget);

    // Verify Empty State text when no transmissions exist
    expect(find.text('AWAITING ACOUSTIC SIGNALS'), findsOneWidget);

    // Verify Protocol Selector Badge in Composer
    expect(find.byType(TextField), findsOneWidget);
    expect(find.text('TX'), findsOneWidget);

    // Tap Settings Button in AppBar
    await tester.tap(find.byIcon(Icons.settings_outlined));
    await tester.pumpAndSettle();

    // Verify Settings Dialog opens
    expect(find.text('STATION SETTINGS'), findsOneWidget);
    expect(find.text('Amateur Radio Callsign (Required for APRS)'), findsOneWidget);

    // Close Dialog
    await tester.tap(find.text('CANCEL'));
    await tester.pumpAndSettle();
    expect(find.text('STATION SETTINGS'), findsNothing);

    // Tap Import Audio Recording Button in AppBar
    await tester.tap(find.byIcon(Icons.file_upload_outlined));
    await tester.pumpAndSettle();

    // Verify Audio Import Dialog opens
    expect(find.text('IMPORT AUDIO RECORDING'), findsOneWidget);
    expect(find.text('Target Demodulator'), findsOneWidget);
    expect(find.text('IMPORT & DECODE'), findsOneWidget);

    // Close Audio Import Dialog
    await tester.tap(find.text('CANCEL'));
    await tester.pumpAndSettle();
    expect(find.text('IMPORT AUDIO RECORDING'), findsNothing);
  });
}
