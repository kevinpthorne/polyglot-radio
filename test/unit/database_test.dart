import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqlite3/sqlite3.dart';
import 'package:polyglot_radio/database/models.dart';
import 'package:polyglot_radio/database/app_database.dart';

void main() {
  group('SQLite AppDatabase Tests', () {
    late AppDatabase database;

    setUp(() {
      final inMemoryDb = sqlite3.openInMemory();
      database = AppDatabase(inMemoryDb);
    });

    tearDown(() {
      database.close();
    });

    test('Insert and retrieve transmissions', () {
      final tx = Transmission(
        id: 'tx-001',
        timestamp: 1600000000000,
        direction: TransmissionDirection.rx,
        protocolId: 'aprs_bell202',
        protocolDisplayName: 'APRS (Bell 202)',
        payloadType: PayloadType.packet,
        textContent: 'K6OTA>APRS:Test packet message',
        rawPayload: Uint8List.fromList([0x01, 0x02, 0x03]),
        snrDb: 18.5,
        durationMs: 450,
        audioFilePath: '/tmp/test.wav',
        isIdentified: true,
      );

      database.insertTransmission(tx);

      final list = database.getTransmissions();
      expect(list.length, equals(1));
      expect(list.first.id, equals('tx-001'));
      expect(list.first.direction, equals(TransmissionDirection.rx));
      expect(list.first.protocolId, equals('aprs_bell202'));
      expect(list.first.textContent, equals('K6OTA>APRS:Test packet message'));
      expect(list.first.snrDb, closeTo(18.5, 0.01));
      expect(list.first.durationMs, equals(450));
      expect(list.first.isIdentified, isTrue);
    });

    test('Ordering and limit works correctly', () {
      for (int i = 1; i <= 5; i++) {
        database.insertTransmission(Transmission(
          id: 'tx-00$i',
          timestamp: 1000 + i * 10,
          direction: TransmissionDirection.tx,
          protocolId: 'cw_morse',
          protocolDisplayName: 'CW (Morse Code)',
          payloadType: PayloadType.text,
          textContent: 'MSG $i',
          snrDb: 25.0,
          durationMs: 200,
          audioFilePath: '',
          isIdentified: true,
        ));
      }

      final top3 = database.getTransmissions(limit: 3);
      expect(top3.length, equals(3));
      // Ordered by timestamp DESC: tx-005, tx-004, tx-003
      expect(top3[0].id, equals('tx-005'));
      expect(top3[1].id, equals('tx-004'));
      expect(top3[2].id, equals('tx-003'));
    });

    test('Reactive watchTransmissions stream emits on new records', () async {
      final stream = database.watchTransmissions();

      expectLater(
        stream,
        emitsInOrder([
          // Initial emission
          predicate<List<Transmission>>((list) => list.isEmpty),
          // Emission after insert
          predicate<List<Transmission>>((list) =>
              list.length == 1 && list.first.id == 'stream-tx-1'),
        ]),
      );

      // Short delay before insert
      await Future.delayed(const Duration(milliseconds: 50));

      database.insertTransmission(Transmission(
        id: 'stream-tx-1',
        timestamp: 1700000000000,
        direction: TransmissionDirection.rx,
        protocolId: 'ultrasound',
        protocolDisplayName: 'Ultrasound 19kHz',
        payloadType: PayloadType.packet,
        textContent: 'ULTRA_SECRET_TOKEN',
        snrDb: 22.0,
        durationMs: 800,
        audioFilePath: '/tmp/ultra.wav',
        isIdentified: true,
      ));
    });

    test('clearAll purges all transmissions from database', () {
      for (int i = 0; i < 5; i++) {
        database.insertTransmission(Transmission(
          id: 'tx-purge-$i',
          timestamp: 1000 + i,
          direction: TransmissionDirection.rx,
          protocolId: 'rattlegram',
          protocolDisplayName: 'Rattlegram (OFDM)',
          payloadType: PayloadType.text,
          textContent: 'Purge test $i',
          snrDb: 15.0,
          durationMs: 500,
          audioFilePath: '',
          isIdentified: true,
        ));
      }
      expect(database.getTransmissions().length, equals(5));

      database.clearAll();
      expect(database.getTransmissions().length, equals(0));
    });

    test('Transmission toJson serializes all fields properly for export', () {
      final tx = Transmission(
        id: 'tx-json-01',
        timestamp: 1710000000000,
        direction: TransmissionDirection.tx,
        protocolId: 'sstv_martin1',
        protocolDisplayName: 'SSTV (Martin 1)',
        payloadType: PayloadType.image,
        textContent: 'SSTV Test',
        imageFilePath: '/path/to/test.png',
        rawPayload: Uint8List.fromList([0xAA, 0xBB]),
        snrDb: 28.5,
        durationMs: 114000,
        audioFilePath: '/audio.wav',
        isIdentified: true,
      );

      final jsonMap = tx.toJson();
      expect(jsonMap['id'], equals('tx-json-01'));
      expect(jsonMap['timestamp'], equals(1710000000000));
      expect(jsonMap['direction'], equals('tx'));
      expect(jsonMap['protocolId'], equals('sstv_martin1'));
      expect(jsonMap['imageFilePath'], equals('/path/to/test.png'));
      expect(jsonMap['snrDb'], equals(28.5));
    });

    test('Fresh database loads default settings with loopback disabled', () {
      final loaded = database.loadStationSettings();
      expect(loaded.isLoopbackEnabled, isFalse);
      expect(loaded.hasValidCallsign, isFalse);
      expect(loaded.squelchThresholdDb, equals(-45.0));
    });

    test('Station settings persistence in SQLite survives reload', () {
      final settings = const StationSettings(
        callsign: 'W1AW',
        stationSymbol: '/#',
        fipsCountyCode: '025001',
        squelchThresholdDb: -52.0,
        isLoopbackEnabled: false,
        rattlegramCarrierFreq: 1500,
        rattlegramSensitivity: 0.35,
        rattlegramMode: 'mode15',
        feldHellCarrierFreq: 1225,
        feldHellMode: 'fsk240',
        easOriginator: 'WXR',
        easEventCode: 'TOR',
        cwPitch: 650.0,
        cwWpm: 25.0,
        sstvMode: 'martin1',
      );

      database.saveStationSettings(settings);

      final loaded = database.loadStationSettings();
      expect(loaded.callsign, equals('W1AW'));
      expect(loaded.hasValidCallsign, isTrue);
      expect(loaded.stationSymbol, equals('/#'));
      expect(loaded.fipsCountyCode, equals('025001'));
      expect(loaded.squelchThresholdDb, closeTo(-52.0, 0.01));
      expect(loaded.isLoopbackEnabled, isFalse);
      expect(loaded.rattlegramCarrierFreq, equals(1500));
      expect(loaded.rattlegramSensitivity, closeTo(0.35, 0.01));
      expect(loaded.rattlegramMode, equals('mode15'));
      expect(loaded.feldHellCarrierFreq, equals(1225));
      expect(loaded.feldHellMode, equals('fsk240'));
      expect(loaded.easOriginator, equals('WXR'));
      expect(loaded.easEventCode, equals('TOR'));
      expect(loaded.cwPitch, closeTo(650.0, 0.01));
      expect(loaded.cwWpm, closeTo(25.0, 0.01));
      expect(loaded.sstvMode, equals('martin1'));
    });
  });
}
