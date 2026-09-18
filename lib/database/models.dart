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

  Transmission copyWith({
    String? id,
    int? timestamp,
    TransmissionDirection? direction,
    String? protocolId,
    String? protocolDisplayName,
    PayloadType? payloadType,
    String? textContent,
    String? imageFilePath,
    Uint8List? rawPayload,
    double? snrDb,
    int? durationMs,
    String? audioFilePath,
    bool? isIdentified,
  }) {
    return Transmission(
      id: id ?? this.id,
      timestamp: timestamp ?? this.timestamp,
      direction: direction ?? this.direction,
      protocolId: protocolId ?? this.protocolId,
      protocolDisplayName: protocolDisplayName ?? this.protocolDisplayName,
      payloadType: payloadType ?? this.payloadType,
      textContent: textContent ?? this.textContent,
      imageFilePath: imageFilePath ?? this.imageFilePath,
      rawPayload: rawPayload ?? this.rawPayload,
      snrDb: snrDb ?? this.snrDb,
      durationMs: durationMs ?? this.durationMs,
      audioFilePath: audioFilePath ?? this.audioFilePath,
      isIdentified: isIdentified ?? this.isIdentified,
    );
  }

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

  Map<String, dynamic> toJson() {
    return {
      'id': id,
      'timestamp': timestamp,
      'direction': direction.name,
      'protocolId': protocolId,
      'protocolDisplayName': protocolDisplayName,
      'payloadType': payloadType.name,
      'textContent': textContent,
      'imageFilePath': imageFilePath,
      'rawPayload': rawPayload?.toList(),
      'snrDb': snrDb,
      'durationMs': durationMs,
      'audioFilePath': audioFilePath,
      'isIdentified': isIdentified,
    };
  }

  factory Transmission.fromJson(Map<String, dynamic> json) {
    return Transmission(
      id: json['id'] as String,
      timestamp: json['timestamp'] as int,
      direction: json['direction'] == 'tx'
          ? TransmissionDirection.tx
          : TransmissionDirection.rx,
      protocolId: json['protocolId'] as String,
      protocolDisplayName: json['protocolDisplayName'] as String,
      payloadType: PayloadType.values.firstWhere(
        (e) => e.name == json['payloadType'],
        orElse: () => PayloadType.unknown,
      ),
      textContent: json['textContent'] as String?,
      imageFilePath: json['imageFilePath'] as String?,
      rawPayload: json['rawPayload'] != null
          ? Uint8List.fromList(List<int>.from(json['rawPayload'] as List))
          : null,
      snrDb: (json['snrDb'] as num?)?.toDouble(),
      durationMs: json['durationMs'] as int,
      audioFilePath: json['audioFilePath'] as String? ?? '',
      isIdentified: json['isIdentified'] as bool? ?? true,
    );
  }
}

class StationSettings {
  final String? callsign; // Defaults to null; required for APRS transmission
  final String stationSymbol;
  final String fipsCountyCode;
  final double squelchThresholdDb;
  final bool isLoopbackEnabled;
  final int rattlegramCarrierFreq;
  final double rattlegramSensitivity;
  final String rattlegramMode;
  final int feldHellCarrierFreq;
  final String feldHellMode;
  final String easOriginator;
  final String easEventCode;
  final double cwPitch;
  final double cwWpm;
  final String sstvMode;

  const StationSettings({
    this.callsign,
    this.stationSymbol = "/-",
    this.fipsCountyCode = "000000",
    this.squelchThresholdDb = -45.0,
    this.isLoopbackEnabled = false,
    this.rattlegramCarrierFreq = 1700,
    this.rattlegramSensitivity = 0.42,
    this.rattlegramMode = 'mode14',
    this.feldHellCarrierFreq = 980,
    this.feldHellMode = 'ook',
    this.easOriginator = 'EAS',
    this.easEventCode = 'RWT',
    this.cwPitch = 700.0,
    this.cwWpm = 20.0,
    this.sstvMode = 'robot36',
  });

  StationSettings copyWith({
    String? callsign,
    bool clearCallsign = false,
    String? stationSymbol,
    String? fipsCountyCode,
    double? squelchThresholdDb,
    bool? isLoopbackEnabled,
    int? rattlegramCarrierFreq,
    double? rattlegramSensitivity,
    String? rattlegramMode,
    int? feldHellCarrierFreq,
    String? feldHellMode,
    String? easOriginator,
    String? easEventCode,
    double? cwPitch,
    double? cwWpm,
    String? sstvMode,
  }) {
    return StationSettings(
      callsign: clearCallsign ? null : (callsign ?? this.callsign),
      stationSymbol: stationSymbol ?? this.stationSymbol,
      fipsCountyCode: fipsCountyCode ?? this.fipsCountyCode,
      squelchThresholdDb: squelchThresholdDb ?? this.squelchThresholdDb,
      isLoopbackEnabled: isLoopbackEnabled ?? this.isLoopbackEnabled,
      rattlegramCarrierFreq: rattlegramCarrierFreq ?? this.rattlegramCarrierFreq,
      rattlegramSensitivity: rattlegramSensitivity ?? this.rattlegramSensitivity,
      rattlegramMode: rattlegramMode ?? this.rattlegramMode,
      feldHellCarrierFreq: feldHellCarrierFreq ?? this.feldHellCarrierFreq,
      feldHellMode: feldHellMode ?? this.feldHellMode,
      easOriginator: easOriginator ?? this.easOriginator,
      easEventCode: easEventCode ?? this.easEventCode,
      cwPitch: cwPitch ?? this.cwPitch,
      cwWpm: cwWpm ?? this.cwWpm,
      sstvMode: sstvMode ?? this.sstvMode,
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
