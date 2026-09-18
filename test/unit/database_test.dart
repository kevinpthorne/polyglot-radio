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
  });
}
