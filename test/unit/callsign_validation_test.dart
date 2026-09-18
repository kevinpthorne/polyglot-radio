import 'package:flutter_test/flutter_test.dart';
import 'package:polyglot_radio/database/models.dart';
import 'package:polyglot_radio/plugins/implementations/all_plugins.dart';
import 'package:polyglot_radio/plugins/modem_plugin.dart';

void main() {
  setUpAll(() {
    registerAllDefaultPlugins();
  });

  group('Callsign Legal Invariant and Validation Tests', () {
    test('Default StationSettings must have null callsign for legal compliance', () {
      const settings = StationSettings();
      expect(settings.callsign, isNull);
      expect(settings.hasValidCallsign, isFalse);
    });

    test('Valid legal amateur callsign formats pass validation', () {
      final validCallsigns = [
        'W1AW',
        'K6OTA',
        'K6OTA-7',
        'N0CALL',
        'AA1AA',
        'JA1ABC',
        'G4XYZ-15',
        'VE3ABC',
        '7L4XYZ',
      ];

      for (final call in validCallsigns) {
        final settings = StationSettings(callsign: call);
        expect(
          settings.hasValidCallsign,
          isTrue,
          reason: 'Expected $call to be recognized as valid callsign',
        );
      }
    });

    test('Non-legal and invalid callsign formats fail validation', () {
      final invalidCallsigns = [
        'POLYGLOT-1', // Not a legal amateur callsign
        'TESTING',
        '12345',
        'W',
        '',
        'INVALID--1',
        'K6OTA-999',
        '@@@',
      ];

      for (final call in invalidCallsigns) {
        final settings = StationSettings(callsign: call);
        expect(
          settings.hasValidCallsign,
          isFalse,
          reason: 'Expected $call to be rejected as invalid amateur callsign',
        );
      }
    });

    test('Amateur packet protocols require callsign, others do not', () {
      final aprs = PluginRegistry.instance.get('aprs_bell202');
      expect(aprs, isNotNull);
      expect(aprs!.requiresCallsign, isTrue);

      final ultrasound = PluginRegistry.instance.get('ultrasound');
      expect(ultrasound, isNotNull);
      expect(ultrasound!.requiresCallsign, isFalse);

      final same = PluginRegistry.instance.get('eas_same');
      expect(same, isNotNull);
      expect(same!.requiresCallsign, isFalse);

      final cw = PluginRegistry.instance.get('cw_morse');
      expect(cw, isNotNull);
      expect(cw!.requiresCallsign, isFalse);
    });
  });
}
