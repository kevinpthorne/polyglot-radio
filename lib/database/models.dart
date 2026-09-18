import 'dart:typed_data';

enum TransmissionDirection {
  rx(0),
  tx(1);

  final int value;
  const TransmissionDirection(this.value);

  static TransmissionDirection fromInt(int v) =>
      v == 1 ? TransmissionDirection.tx : TransmissionDirection.rx;
}

enum PayloadType {
  text(0),
  image(1),
  packet(2),
  unknown(3);

  final int value;
  const PayloadType(this.value);

  static PayloadType fromInt(int v) {
    return PayloadType.values.firstWhere(
      (e) => e.value == v,
      orElse: () => PayloadType.unknown,
    );
  }
}

class Transmission {
  final String id;
  final int timestamp;
  final TransmissionDirection direction;
  final String protocolId;
  final String protocolDisplayName;
  final PayloadType payloadType;
  final String? textContent;
  final String? imageFilePath;
  final Uint8List? rawPayload;
  final double? snrDb;
  final int durationMs;
  final String audioFilePath;
  final bool isIdentified;

  const Transmission({
    required this.id,
    required this.timestamp,
    required this.direction,
    required this.protocolId,
    required this.protocolDisplayName,
    required this.payloadType,
    this.textContent,
    this.imageFilePath,
    this.rawPayload,
    this.snrDb,
    required this.durationMs,
    required this.audioFilePath,
    this.isIdentified = true,
  });

  Map<String, dynamic> toMap() {
    return {
      'id': id,
      'timestamp': timestamp,
      'direction': direction.value,
      'protocol_id': protocolId,
      'protocol_display_name': protocolDisplayName,
      'payload_type': payloadType.value,
      'text_content': textContent,
      'image_file_path': imageFilePath,
      'raw_payload': rawPayload,
      'snr_db': snrDb,
      'duration_ms': durationMs,
      'audio_file_path': audioFilePath,
      'is_identified': isIdentified ? 1 : 0,
    };
  }

  factory Transmission.fromMap(Map<String, dynamic> map) {
    return Transmission(
      id: map['id'] as String,
      timestamp: map['timestamp'] as int,
      direction: TransmissionDirection.fromInt(map['direction'] as int),
      protocolId: map['protocol_id'] as String,
      protocolDisplayName: map['protocol_display_name'] as String,
      payloadType: PayloadType.fromInt(map['payload_type'] as int),
      textContent: map['text_content'] as String?,
      imageFilePath: map['image_file_path'] as String?,
      rawPayload: map['raw_payload'] as Uint8List?,
      snrDb: (map['snr_db'] as num?)?.toDouble(),
      durationMs: map['duration_ms'] as int,
      audioFilePath: map['audio_file_path'] as String,
      isIdentified: (map['is_identified'] as int? ?? 1) == 1,
    );
  }
}

class StationSettings {
  final String? callsign; // Defaults to null; required for APRS transmission
  final String stationSymbol;
  final String fipsCountyCode;
  final double squelchThresholdDb;
  final bool isLoopbackEnabled;

  const StationSettings({
    this.callsign,
    this.stationSymbol = "/-",
    this.fipsCountyCode = "000000",
    this.squelchThresholdDb = -45.0,
    this.isLoopbackEnabled = true,
  });

  StationSettings copyWith({
    String? callsign,
    bool clearCallsign = false,
    String? stationSymbol,
    String? fipsCountyCode,
    double? squelchThresholdDb,
    bool? isLoopbackEnabled,
  }) {
    return StationSettings(
      callsign: clearCallsign ? null : (callsign ?? this.callsign),
      stationSymbol: stationSymbol ?? this.stationSymbol,
      fipsCountyCode: fipsCountyCode ?? this.fipsCountyCode,
      squelchThresholdDb: squelchThresholdDb ?? this.squelchThresholdDb,
      isLoopbackEnabled: isLoopbackEnabled ?? this.isLoopbackEnabled,
    );
  }

  bool get hasValidCallsign {
    if (callsign == null || callsign!.trim().isEmpty) return false;
    // Standard amateur radio callsign format validation (1-2 letters/digits + digit + 1-3 letters, optional -SSID)
    final trimmed = callsign!.trim().toUpperCase();
    final regex = RegExp(r'^[A-Z0-9]{1,3}\d[A-Z]{1,4}(-\d{1,2})?$');
    return regex.hasMatch(trimmed);
  }
}
