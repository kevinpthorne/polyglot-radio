import 'dart:typed_data';
import '../database/models.dart';

enum PayloadCategory {
  image,
  textStream,
  packet,
  inaudible;

  PayloadType toPayloadType() {
    switch (this) {
      case PayloadCategory.image:
        return PayloadType.image;
      case PayloadCategory.textStream:
        return PayloadType.text;
      case PayloadCategory.packet:
        return PayloadType.packet;
      case PayloadCategory.inaudible:
        return PayloadType.packet;
    }
  }
}

abstract class ModemProtocolPlugin {
  String get id;
  String get displayName;
  String get description;
  PayloadCategory get category;
  bool get requiresCallsign;

  Map<String, dynamic> getDefaultConfig();
  String formatPayload(Uint8List payload);
}

class PluginRegistry {
  static final PluginRegistry instance = PluginRegistry._internal();
  final Map<String, ModemProtocolPlugin> _plugins = {};

  PluginRegistry._internal();

  void register(ModemProtocolPlugin plugin) {
    _plugins[plugin.id] = plugin;
  }

  ModemProtocolPlugin? get(String id) {
    if (_plugins.containsKey(id)) return _plugins[id];
    if (id == 'aprs_bell202') return _plugins['aprs_packet'];
    if (id.startsWith('sstv_')) return _plugins['sstv_engine'];
    if (id == 'olivia') return _plugins['olivia_mfsk'];
    if (id == 'wefax') return _plugins['hf_wefax'];
    if (id == 'ft8') return _plugins['ft8_engine'];
    if (id == 'cw') return _plugins['cw_morse'];
    if (id == 'same') return _plugins['eas_same'];
    if (id == 'hell') return _plugins['feld_hell'];
    return null;
  }
  List<ModemProtocolPlugin> get all => _plugins.values.toList();
  List<ModemProtocolPlugin> getAll() => all;

  List<ModemProtocolPlugin> getSelectableProtocols({
    required bool hasImage,
    required bool hasText,
  }) {
    return _plugins.values.where((plugin) {
      if (hasImage) {
        return plugin.category == PayloadCategory.image;
      }
      if (hasText) {
        return plugin.category == PayloadCategory.textStream ||
            plugin.category == PayloadCategory.packet ||
            plugin.category == PayloadCategory.inaudible;
      }
      return true;
    }).toList();
  }
}
