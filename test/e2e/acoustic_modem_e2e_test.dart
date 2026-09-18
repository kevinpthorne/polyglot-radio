import 'dart:io';
import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqlite3/sqlite3.dart';
import 'package:polyglot_radio/core/modem_coordinator.dart';
import 'package:polyglot_radio/database/app_database.dart';
import 'package:polyglot_radio/database/models.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('Acoustic Modem End-to-End Testing (E2E Loopback)', () {
    late ModemCoordinator coordinator;
    late AppDatabase memoryDb;

    setUpAll(() async {
      memoryDb = AppDatabase(sqlite3.openInMemory());
      coordinator = ModemCoordinator.instance;
      await coordinator.initialize(
        customDb: memoryDb,
        enableLoopback: true,
      );
    });

    tearDownAll(() {
      coordinator.dispose();
    });

    test('E2E Invariant: Legal amateur callsign enforcement blocks APRS transmission', () async {
      // Ensure callsign is null
      coordinator.updateSettings(const StationSettings(callsign: null));
      expect(coordinator.settingsNotifier.value.callsign, isNull);
      expect(coordinator.settingsNotifier.value.hasValidCallsign, isFalse);

      // Attempt transmitting APRS packet without callsign
      expect(
        () async => await coordinator.transmit(
          protocolId: 'aprs_bell202',
          text: 'CQ DE UNLICENSED',
        ),
        throwsA(isA<CallsignRequiredException>()),
      );

      // Verify no records inserted in DB
      final records = memoryDb.getTransmissions();
      expect(records.isEmpty, isTrue);
    });

    test('E2E Permitted: Ultrasound does not require amateur callsign', () async {
      // Callsign remains null
      expect(coordinator.settingsNotifier.value.callsign, isNull);

      final success = await coordinator.transmit(
        protocolId: 'ultrasound',
        text: 'PROPRIETARY_DEVICE_SYNC_0xAA',
      );
      expect(success, isTrue);

      // Verify transmission was recorded in DB
      final records = memoryDb.getTransmissions();
      expect(records.isNotEmpty, isTrue);
      expect(records.first.protocolId, equals('ultrasound'));
      expect(records.first.direction, equals(TransmissionDirection.tx));
      expect(records.first.textContent, equals('PROPRIETARY_DEVICE_SYNC_0xAA'));
    });

    test('E2E Licensed: Setting valid callsign enables APRS transmission', () async {
      coordinator.updateSettings(const StationSettings(
        callsign: 'K6OTA-7',
        stationSymbol: '/>',
        isLoopbackEnabled: true,
      ));
      expect(coordinator.settingsNotifier.value.hasValidCallsign, isTrue);

      final success = await coordinator.transmit(
        protocolId: 'aprs_bell202',
        text: 'K6OTA-7>APRS:=3745.00N/12227.00W>Acoustic APRS Packet',
      );
      expect(success, isTrue);

      // Wait a short time for TX completion monitoring
      await Future.delayed(const Duration(milliseconds: 200));

      final records = memoryDb.getTransmissions();
      final aprsRecord = records.firstWhere((r) => r.protocolId == 'aprs_bell202');
      expect(aprsRecord.direction, equals(TransmissionDirection.tx));
      expect(aprsRecord.textContent, contains('Acoustic APRS Packet'));
    });

    test('E2E CW Morse Transmission Logs and Database Audit Trail', () async {
      final success = await coordinator.transmit(
        protocolId: 'cw_morse',
        text: 'CQ CQ CQ DE K6OTA K',
      );
      expect(success, isTrue);

      await Future.delayed(const Duration(milliseconds: 150));

      final records = memoryDb.getTransmissions();
      final cwRecord = records.firstWhere((r) => r.protocolId == 'cw_morse');
      expect(cwRecord.direction, equals(TransmissionDirection.tx));
      expect(cwRecord.textContent, equals('CQ CQ CQ DE K6OTA K'));
      expect(cwRecord.payloadType, equals(PayloadType.text));
    });

    test('E2E Reprocessing Pipeline API', () {
      final success = coordinator.reprocessRecording(
        wavPath: 'nonexistent_test.wav',
        targetModemId: 'aprs_bell202',
      );
      // Fails gracefully because wav does not exist, but verifies FFI call integrity
      expect(success, isFalse);
    });

    test('E2E Pre-saved Audio File Import and Demodulation Pipeline', () async {
      // 1. Synthesize a 44-byte standard PCM WAV file
      final sampleRate = 48000;
      final totalSamples = 4800; // 100ms
      final wavBytes = BytesBuilder();
      wavBytes.add(const [0x52, 0x49, 0x46, 0x46]); // "RIFF"
      final b = ByteData(4)..setUint32(0, 36 + totalSamples * 2, Endian.little);
      wavBytes.add(b.buffer.asUint8List());
      wavBytes.add(const [0x57, 0x41, 0x56, 0x45]); // "WAVE"
      wavBytes.add(const [0x66, 0x6D, 0x74, 0x20]); // "fmt "
      wavBytes.add(const [16, 0, 0, 0]);
      wavBytes.add(const [1, 0]); // PCM
      wavBytes.add(const [1, 0]); // Mono
      final srData = ByteData(4)..setUint32(0, sampleRate, Endian.little);
      wavBytes.add(srData.buffer.asUint8List());
      final brData = ByteData(4)..setUint32(0, sampleRate * 2, Endian.little);
      wavBytes.add(brData.buffer.asUint8List());
      wavBytes.add(const [2, 0]);
      wavBytes.add(const [16, 0]);
      wavBytes.add(const [0x64, 0x61, 0x74, 0x61]); // "data"
      final dsData = ByteData(4)..setUint32(0, totalSamples * 2, Endian.little);
      wavBytes.add(dsData.buffer.asUint8List());
      wavBytes.add(Uint8List(totalSamples * 2)); // Silence samples

      final tempWav = File('/tmp/e2e_test_import.wav');
      await tempWav.writeAsBytes(wavBytes.toBytes());

      final initialCount = memoryDb.getTransmissions().length;

      // 2. Import into app
      final success = await coordinator.importAudioRecording(
        sourceFilePath: tempWav.path,
        targetModemId: 'cw_morse',
      );
      expect(success, isTrue);

      // 3. Verify record was created in database
      final updatedRecords = memoryDb.getTransmissions();
      expect(updatedRecords.length, equals(initialCount + 1));
      final importedRecord = updatedRecords.first;
      expect(importedRecord.direction, equals(TransmissionDirection.rx));
      expect(importedRecord.audioFilePath, contains('imported_'));
      expect(File(importedRecord.audioFilePath).existsSync(), isTrue);

      // Clean up temp file
      if (tempWav.existsSync()) tempWav.deleteSync();
      if (File(importedRecord.audioFilePath).existsSync()) {
        File(importedRecord.audioFilePath).deleteSync();
      }
    });
  });
}
