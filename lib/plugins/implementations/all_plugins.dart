import 'dart:convert';
import 'dart:typed_data';
import '../modem_plugin.dart';

class SstvPlugin extends ModemProtocolPlugin {
  @override
  String get id => 'sstv_engine';
  @override
  String get displayName => 'SSTV (Slow Scan TV)';
  @override
  String get description => 'Analog color FM picture sweeps (Martin, Scottie, Robot, PD)';
  @override
  PayloadCategory get category => PayloadCategory.image;
  @override
  bool get requiresCallsign => false;

  @override
  Map<String, dynamic> getDefaultConfig() => {
        'mode': 'Robot36',
        'width': 320,
        'height': 240,
      };

  @override
  String formatPayload(Uint8List payload) => '[SSTV Image]';
}

class RattlegramPlugin extends ModemProtocolPlugin {
  @override
  String get id => 'rattlegram';
  @override
  String get displayName => 'Rattlegram (COFDM)';
  @override
  String get description => '88-subcarrier COFDM high-speed acoustic text and binary transfer';
  @override
  PayloadCategory get category => PayloadCategory.packet;
  @override
  bool get requiresCallsign => false;

  @override
  Map<String, dynamic> getDefaultConfig() => {'modulation': 'QPSK'};

  @override
  String formatPayload(Uint8List payload) => utf8.decode(payload, allowMalformed: true);
}

class AprsPlugin extends ModemProtocolPlugin {
  @override
  String get id => 'aprs_packet';
  @override
  String get displayName => 'APRS / AX.25 (Bell 202)';
  @override
  String get description => '1200-baud AFSK packet radio. Strictly requires legal amateur callsign.';
  @override
  PayloadCategory get category => PayloadCategory.packet;
  @override
  bool get requiresCallsign => true; // STRICT LEGAL REQUIREMENT

  @override
  Map<String, dynamic> getDefaultConfig() => {
        'callsign': '',
        'symbol': '/-',
      };

  @override
  String formatPayload(Uint8List payload) => utf8.decode(payload, allowMalformed: true);
}

class EasSamePlugin extends ModemProtocolPlugin {
  @override
  String get id => 'eas_same';
  @override
  String get displayName => 'EAS / SAME Alert';
  @override
  String get description => 'Emergency Alert System (NOAA Weather & Disaster Warning AFSK data bursts: ZCZC-...)';
  @override
  PayloadCategory get category => PayloadCategory.packet;
  @override
  bool get requiresCallsign => false;

  @override
  Map<String, dynamic> getDefaultConfig() => {
        'event_code': 'RWT',
        'fips': '000000',
      };

  @override
  String formatPayload(Uint8List payload) => utf8.decode(payload, allowMalformed: true);
}

class CwMorsePlugin extends ModemProtocolPlugin {
  @override
  String get id => 'cw_morse';
  @override
  String get displayName => 'CW Morse Code';
  @override
  String get description => 'Continuous tone telegraphy with raised-cosine click-free envelope (5–45 WPM)';
  @override
  PayloadCategory get category => PayloadCategory.textStream;
  @override
  bool get requiresCallsign => false;

  @override
  Map<String, dynamic> getDefaultConfig() => {
        'pitch': 700,
        'wpm': 20,
      };

  @override
  String formatPayload(Uint8List payload) => utf8.decode(payload, allowMalformed: true);
}

class Psk31Plugin extends ModemProtocolPlugin {
  @override
  String get id => 'psk31';
  @override
  String get displayName => 'PSK31 (BPSK)';
  @override
  String get description => 'Narrowband 31.25-baud phase-shift keying with Varicode framing';
  @override
  PayloadCategory get category => PayloadCategory.textStream;
  @override
  bool get requiresCallsign => false;

  @override
  Map<String, dynamic> getDefaultConfig() => {'center_freq': 1000};

  @override
  String formatPayload(Uint8List payload) => utf8.decode(payload, allowMalformed: true);
}

class FeldHellPlugin extends ModemProtocolPlugin {
  @override
  String get id => 'feld_hell';
  @override
  String get displayName => 'Feld Hell (Hellschreiber)';
  @override
  String get description => '122.5 Hz dot-matrix visual facsimile telegraphy rendered to chat frame';
  @override
  PayloadCategory get category => PayloadCategory.image;
  @override
  bool get requiresCallsign => false;

  @override
  Map<String, dynamic> getDefaultConfig() => {'dot_clock': 122.5};

  @override
  String formatPayload(Uint8List payload) => '[Feld-Hell Facsimile]';
}

class OliviaPlugin extends ModemProtocolPlugin {
  @override
  String get id => 'olivia_mfsk';
  @override
  String get displayName => 'Olivia MFSK';
  @override
  String get description => '16-tone orthogonal frequency-shift keying with Fast Walsh-Hadamard correlation';
  @override
  PayloadCategory get category => PayloadCategory.textStream;
  @override
  bool get requiresCallsign => false;

  @override
  Map<String, dynamic> getDefaultConfig() => {'tones': 16, 'bandwidth': 500};

  @override
  String formatPayload(Uint8List payload) => utf8.decode(payload, allowMalformed: true);
}

class WefaxPlugin extends ModemProtocolPlugin {
  @override
  String get id => 'hf_wefax';
  @override
  String get displayName => 'HF WEFAX (120 LPM Fax)';
  @override
  String get description => 'Analog maritime weather facsimile (120 lines/min) FM discriminator';
  @override
  PayloadCategory get category => PayloadCategory.image;
  @override
  bool get requiresCallsign => false;

  @override
  Map<String, dynamic> getDefaultConfig() => {'lpm': 120};

  @override
  String formatPayload(Uint8List payload) => '[WEFAX Weather Chart]';
}

class Ft8Plugin extends ModemProtocolPlugin {
  @override
  String get id => 'ft8_engine';
  @override
  String get displayName => 'FT8 (8-GFSK Weak Signal)';
  @override
  String get description => '8-GFSK Costas array synchronization and LDPC(174,87) framing';
  @override
  PayloadCategory get category => PayloadCategory.packet;
  @override
  bool get requiresCallsign => false;

  @override
  Map<String, dynamic> getDefaultConfig() => {'baud': 6.25};

  @override
  String formatPayload(Uint8List payload) => utf8.decode(payload, allowMalformed: true);
}

class UltrasoundPlugin extends ModemProtocolPlugin {
  @override
  String get id => 'ultrasound';
  @override
  String get displayName => 'Ultrasound (19 kHz Silent)';
  @override
  String get description => 'Inaudible 18.5–19.8 kHz near-ultrasonic multi-FSK with Reed-Solomon FEC';
  @override
  PayloadCategory get category => PayloadCategory.inaudible;
  @override
  bool get requiresCallsign => false;

  @override
  Map<String, dynamic> getDefaultConfig() => {'freq_band': '18.5-19.8kHz'};

  @override
  String formatPayload(Uint8List payload) => utf8.decode(payload, allowMalformed: true);
}

void registerAllDefaultPlugins() {
  final reg = PluginRegistry.instance;
  reg.register(SstvPlugin());
  reg.register(RattlegramPlugin());
  reg.register(AprsPlugin());
  reg.register(EasSamePlugin());
  reg.register(CwMorsePlugin());
  reg.register(Psk31Plugin());
  reg.register(FeldHellPlugin());
  reg.register(OliviaPlugin());
  reg.register(WefaxPlugin());
  reg.register(Ft8Plugin());
  reg.register(UltrasoundPlugin());
}
