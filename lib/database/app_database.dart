import 'dart:async';
import 'dart:io';
import 'dart:typed_data';
import 'package:sqlite3/sqlite3.dart';
import 'package:path_provider/path_provider.dart';
import 'package:path/path.dart' as p;
import 'models.dart';

class AppDatabase {
  final Database db;
  final _changeController = StreamController<List<Transmission>>.broadcast();

  AppDatabase(this.db) {
    _createTables();
  }

  static Future<AppDatabase> openDefault() async {
    final docsDir = await getApplicationDocumentsDirectory();
    final dbDir = Directory(p.join(docsDir.path, 'polyglot_radio'));
    if (!dbDir.existsSync()) {
      dbDir.createSync(recursive: true);
    }
    final dbPath = p.join(dbDir.path, 'logbook.sqlite');
    final database = sqlite3.open(dbPath);
    return AppDatabase(database);
  }

  static AppDatabase inMemory() {
    final database = sqlite3.openInMemory();
    return AppDatabase(database);
  }

  void _createTables() {
    db.execute('''
      CREATE TABLE IF NOT EXISTS transmissions (
        id TEXT PRIMARY KEY NOT NULL,
        timestamp INTEGER NOT NULL,
        direction INTEGER NOT NULL,
        protocol_id TEXT NOT NULL,
        protocol_display_name TEXT NOT NULL,
        payload_type INTEGER NOT NULL,
        text_content TEXT,
        image_file_path TEXT,
        raw_payload BLOB,
        snr_db REAL,
        duration_ms INTEGER NOT NULL,
        audio_file_path TEXT NOT NULL,
        is_identified INTEGER NOT NULL DEFAULT 1
      );
    ''');
    db.execute('''
      CREATE INDEX IF NOT EXISTS idx_transmissions_timestamp 
      ON transmissions(timestamp DESC);
    ''');
  }

  void insertTransmission(Transmission t) {
    final stmt = db.prepare('''
      INSERT OR REPLACE INTO transmissions (
        id, timestamp, direction, protocol_id, protocol_display_name,
        payload_type, text_content, image_file_path, raw_payload,
        snr_db, duration_ms, audio_file_path, is_identified
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''');

    stmt.execute([
      t.id,
      t.timestamp,
      t.direction.value,
      t.protocolId,
      t.protocolDisplayName,
      t.payloadType.value,
      t.textContent,
      t.imageFilePath,
      t.rawPayload,
      t.snrDb,
      t.durationMs,
      t.audioFilePath,
      t.isIdentified ? 1 : 0,
    ]);
    stmt.close();
    _notifyChanges();
  }

  List<Transmission> getTransmissions({int limit = 100}) {
    final ResultSet results = db.select(
      'SELECT * FROM transmissions ORDER BY timestamp DESC LIMIT ?',
      [limit],
    );

    return results.map((row) {
      return Transmission(
        id: row['id'] as String,
        timestamp: row['timestamp'] as int,
        direction: TransmissionDirection.fromInt(row['direction'] as int),
        protocolId: row['protocol_id'] as String,
        protocolDisplayName: row['protocol_display_name'] as String,
        payloadType: PayloadType.fromInt(row['payload_type'] as int),
        textContent: row['text_content'] as String?,
        imageFilePath: row['image_file_path'] as String?,
        rawPayload: row['raw_payload'] as Uint8List?,
        snrDb: (row['snr_db'] as num?)?.toDouble(),
        durationMs: row['duration_ms'] as int,
        audioFilePath: row['audio_file_path'] as String,
        isIdentified: (row['is_identified'] as int? ?? 1) == 1,
      );
    }).toList();
  }

  Transmission? getTransmissionById(String id) {
    final ResultSet results = db.select(
      'SELECT * FROM transmissions WHERE id = ?',
      [id],
    );
    if (results.isEmpty) return null;
    final row = results.first;
    return Transmission(
      id: row['id'] as String,
      timestamp: row['timestamp'] as int,
      direction: TransmissionDirection.fromInt(row['direction'] as int),
      protocolId: row['protocol_id'] as String,
      protocolDisplayName: row['protocol_display_name'] as String,
      payloadType: PayloadType.fromInt(row['payload_type'] as int),
      textContent: row['text_content'] as String?,
      imageFilePath: row['image_file_path'] as String?,
      rawPayload: row['raw_payload'] as Uint8List?,
      snrDb: (row['snr_db'] as num?)?.toDouble(),
      durationMs: row['duration_ms'] as int,
      audioFilePath: row['audio_file_path'] as String,
      isIdentified: (row['is_identified'] as int? ?? 1) == 1,
    );
  }

  void deleteTransmission(String id) {
    db.execute('DELETE FROM transmissions WHERE id = ?', [id]);
    _notifyChanges();
  }

  void clearAll() {
    db.execute('DELETE FROM transmissions');
    _notifyChanges();
  }

  Stream<List<Transmission>> watchTransmissions() {
    // Emit current list first, then listen to changes
    return Stream.multi((controller) {
      controller.add(getTransmissions());
      final sub = _changeController.stream.listen(controller.add);
      controller.onCancel = () => sub.cancel();
    });
  }

  void _notifyChanges() {
    if (!_changeController.isClosed) {
      _changeController.add(getTransmissions());
    }
  }

  void close() {
    _changeController.close();
    db.close();
  }
}
