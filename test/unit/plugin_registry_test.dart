import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:polyglot_radio/plugins/implementations/all_plugins.dart';
import 'package:polyglot_radio/plugins/modem_plugin.dart';

void main() {
  setUpAll(() {
    registerAllDefaultPlugins();
  });

  group('Modem Plugin Architecture and Registry Tests', () {
    test('All 11 modem protocol families are registered and retrievable', () {
      final expectedProtocols = [
        'sstv_martin1',
        'sstv_martin2',
        'sstv_scottie1',
        'sstv_scottie2',
        'sstv_robot36',
        'sstv_pd120',
        'aprs_bell202',
        'eas_same',
        'cw_morse',
        'psk31',
        'feld_hell',
        'olivia',
        'wefax',
        'ft8',
        'ultrasound',
      ];

      for (final protoId in expectedProtocols) {
        final plugin = PluginRegistry.instance.get(protoId);
        expect(plugin, isNotNull, reason: 'Expected protocol $protoId to be registered');
        expect(plugin!.displayName.isNotEmpty, isTrue);
        expect(plugin.description.isNotEmpty, isTrue);
      }
    });

    test('Protocol selection filtering matches media payload requirements', () {
      // If user attaches an image, only image-capable protocols (SSTV / WEFAX) should be selectable
      final imageProtocols = PluginRegistry.instance.getSelectableProtocols(
        hasImage: true,
        hasText: false,
      );
      expect(imageProtocols.isNotEmpty, isTrue);
      for (final p in imageProtocols) {
        expect(p.category, equals(PayloadCategory.image),
            reason: 'Only image modems should accept images');
      }

      // If text only, text, packet, and inaudible modems should be selectable
      final textProtocols = PluginRegistry.instance.getSelectableProtocols(
        hasImage: false,
        hasText: true,
      );
      expect(textProtocols.isNotEmpty, isTrue);
      for (final p in textProtocols) {
        expect(p.category != PayloadCategory.image, isTrue,
            reason: 'Image modems without text mode should not accept text-only');
      }
    });

    test('Plugin formatPayload correctly decodes payload bytes', () {
      final cw = PluginRegistry.instance.get('cw_morse')!;
      final textBytes = Uint8List.fromList(utf8.encode('CQ CQ TEST'));
      expect(cw.formatPayload(textBytes), equals('CQ CQ TEST'));

      final aprs = PluginRegistry.instance.get('aprs_bell202')!;
      final aprsBytes = Uint8List.fromList(utf8.encode('K6OTA>APRS:=3745.00N/12227.00W-'));
      expect(aprs.formatPayload(aprsBytes), equals('K6OTA>APRS:=3745.00N/12227.00W-'));
    });
  });
}
