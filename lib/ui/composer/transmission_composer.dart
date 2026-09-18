import 'dart:typed_data';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;
import '../../core/modem_coordinator.dart';
import '../../plugins/modem_plugin.dart';
import '../settings/station_settings_dialog.dart';
import '../settings/modem_params_dialog.dart';
import 'protocol_picker_sheet.dart';

class TransmissionComposer extends StatefulWidget {
  const TransmissionComposer({super.key});

  @override
  State<TransmissionComposer> createState() => _TransmissionComposerState();
}

class _TransmissionComposerState extends State<TransmissionComposer> {
  final TextEditingController _textController = TextEditingController();
  late ModemProtocolPlugin _selectedPlugin;
  String? _selectedImagePath;
  bool _isHexMode = false;

  @override
  void initState() {
    super.initState();
    // Default to Ultrasound (inaudible, no callsign needed) or CW
    _selectedPlugin = PluginRegistry.instance.get('ultrasound') ??
        PluginRegistry.instance.getAll().first;
  }

  @override
  void dispose() {
    _textController.dispose();
    super.dispose();
  }

  Uint8List? _parseHex(String text) {
    final cleaned = text.replaceAll(RegExp(r'\s+|0x|,|:'), '');
    if (cleaned.isEmpty) return Uint8List(0);
    if (cleaned.length % 2 != 0) return null;
    try {
      final result = Uint8List(cleaned.length ~/ 2);
      for (int i = 0; i < cleaned.length; i += 2) {
        result[i ~/ 2] = int.parse(cleaned.substring(i, i + 2), radix: 16);
      }
      return result;
    } catch (_) {
      return null;
    }
  }

  Future<void> _pickImageFile() async {
    try {
      final files = await FilePicker.pickFiles(
        type: FileType.image,
        dialogTitle: 'Select Image for SSTV / WEFAX',
      );
      if (files.isNotEmpty && files.first.path != null) {
        final path = files.first.path!;
        setState(() {
          _selectedImagePath = path;
          if (_selectedPlugin.category != PayloadCategory.image) {
            _selectedPlugin = PluginRegistry.instance.get('sstv_martin1') ??
                PluginRegistry.instance.get('wefax_576') ??
                _selectedPlugin;
          }
        });
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text("Could not open file picker: $e")),
        );
      }
    }
  }

  void _enterImagePathDialog() {
    final controller = TextEditingController(text: _selectedImagePath ?? '');
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: const Color(0xFF161B22),
        title: const Text("Enter Image File Path",
            style: TextStyle(color: Colors.white, fontSize: 16)),
        content: TextField(
          controller: controller,
          style: const TextStyle(
              color: Colors.white, fontFamily: 'monospace', fontSize: 13),
          decoration: const InputDecoration(
            hintText: "/path/to/image.png",
            hintStyle: TextStyle(color: Colors.white30),
            enabledBorder: UnderlineInputBorder(
                borderSide: BorderSide(color: Colors.white24)),
            focusedBorder: UnderlineInputBorder(
                borderSide: BorderSide(color: Colors.cyanAccent)),
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child:
                const Text("Cancel", style: TextStyle(color: Colors.white60)),
          ),
          ElevatedButton(
            onPressed: () {
              final path = controller.text.trim();
              if (path.isNotEmpty) {
                setState(() {
                  _selectedImagePath = path;
                  if (_selectedPlugin.category != PayloadCategory.image) {
                    _selectedPlugin =
                        PluginRegistry.instance.get('sstv_martin1') ??
                            PluginRegistry.instance.get('wefax_576') ??
                            _selectedPlugin;
                  }
                });
              }
              Navigator.pop(ctx);
            },
            child: const Text("Apply"),
          ),
        ],
      ),
    );
  }

  void _showProtocolPicker() {
    showModalBottomSheet(
      context: context,
      backgroundColor: Colors.transparent,
      builder: (ctx) => ProtocolPickerSheet(
        currentSelection: _selectedPlugin,
        hasImage: _selectedImagePath != null,
        hasText: _textController.text.isNotEmpty,
        onSelected: (plugin) {
          setState(() {
            _selectedPlugin = plugin;
          });
        },
      ),
    );
  }

  Future<void> _handleTransmit() async {
    final text = _textController.text.trim();
    Uint8List? rawBytes;
    if (_isHexMode && text.isNotEmpty) {
      final parsed = _parseHex(text);
      if (parsed == null) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text(
                "Invalid hexadecimal format. Enter hex pairs e.g. '01 A4 FF'."),
            backgroundColor: Colors.redAccent,
          ),
        );
        return;
      }
      rawBytes = parsed;
    }

    if (text.isEmpty && _selectedImagePath == null && rawBytes == null) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text("Enter a message or attach an image to transmit."),
          duration: Duration(seconds: 2),
        ),
      );
      return;
    }

    final coordinator = ModemCoordinator.instance;
    final settings = coordinator.settingsNotifier.value;

    // Check legal requirement
    if (_selectedPlugin.requiresCallsign && !settings.hasValidCallsign) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          backgroundColor: const Color(0xFF5A1A1A),
          content: Text(
            'Legal requirement: "${_selectedPlugin.displayName}" requires an amateur radio callsign.',
            style: const TextStyle(color: Colors.white),
          ),
          action: SnackBarAction(
            label: 'SETTINGS',
            textColor: Colors.amberAccent,
            onPressed: () {
              showDialog(
                context: context,
                builder: (ctx) => const StationSettingsDialog(),
              );
            },
          ),
          duration: const Duration(seconds: 5),
        ),
      );
      return;
    }

    try {
      final success = await coordinator.transmit(
        protocolId: _selectedPlugin.id,
        text: text,
        rawBytes: rawBytes,
        imageFilePath: _selectedImagePath,
      );

      if (success) {
        _textController.clear();
        setState(() {
          _selectedImagePath = null;
        });
      } else {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(
              content: Text("Transmission failed to start."),
              backgroundColor: Colors.redAccent,
            ),
          );
        }
      }
    } on CallsignRequiredException catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            backgroundColor: const Color(0xFF5A1A1A),
            content: Text(e.message, style: const TextStyle(color: Colors.white)),
            action: SnackBarAction(
              label: 'SETTINGS',
              textColor: Colors.amberAccent,
              onPressed: () {
                showDialog(
                  context: context,
                  builder: (ctx) => const StationSettingsDialog(),
                );
              },
            ),
          ),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text("Error transmitting: $e")),
        );
      }
    }
  }

  void _showImageOptions() {
    showModalBottomSheet(
      context: context,
      backgroundColor: const Color(0xFF161B22),
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(16)),
      ),
      builder: (ctx) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 20, horizontal: 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              "IMAGE PAYLOAD (SSTV / WEFAX)",
              style: TextStyle(
                color: Colors.cyanAccent,
                fontWeight: FontWeight.bold,
                fontFamily: 'monospace',
                fontSize: 12,
              ),
            ),
            const SizedBox(height: 12),
            ListTile(
              leading: const Icon(Icons.file_upload, color: Colors.cyanAccent),
              title: const Text("Choose Image File...",
                  style: TextStyle(color: Colors.white)),
              subtitle: const Text("Select PNG, JPG, or BMP from disk",
                  style: TextStyle(color: Colors.white60, fontSize: 11)),
              onTap: () {
                Navigator.pop(ctx);
                _pickImageFile();
              },
            ),
            ListTile(
              leading: const Icon(Icons.edit_note, color: Colors.blueAccent),
              title: const Text("Enter Image Path...",
                  style: TextStyle(color: Colors.white)),
              subtitle: const Text("Specify absolute or relative path to file",
                  style: TextStyle(color: Colors.white60, fontSize: 11)),
              onTap: () {
                Navigator.pop(ctx);
                _enterImagePathDialog();
              },
            ),
            ListTile(
              leading: const Icon(Icons.grid_on, color: Colors.tealAccent),
              title: const Text("Use Standard Color Test Pattern",
                  style: TextStyle(color: Colors.white)),
              subtitle: const Text("Generates calibration color bars for SSTV / WEFAX",
                  style: TextStyle(color: Colors.white60, fontSize: 11)),
              onTap: () {
                setState(() {
                  _selectedImagePath = "synthetic_color_bars.png";
                  if (_selectedPlugin.category != PayloadCategory.image) {
                    _selectedPlugin = PluginRegistry.instance.get('sstv_martin1') ??
                        PluginRegistry.instance.get('wefax_576') ??
                        _selectedPlugin;
                  }
                });
                Navigator.pop(ctx);
              },
            ),
            if (_selectedImagePath != null)
              ListTile(
                leading: const Icon(Icons.delete_outline, color: Colors.redAccent),
                title: const Text("Remove Image Attachment",
                    style: TextStyle(color: Colors.redAccent)),
                onTap: () {
                  setState(() {
                    _selectedImagePath = null;
                  });
                  Navigator.pop(ctx);
                },
              ),
          ],
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final coordinator = ModemCoordinator.instance;

    return ValueListenableBuilder<bool>(
      valueListenable: coordinator.isTransmittingNotifier,
      builder: (context, isTx, _) {
        return ValueListenableBuilder(
          valueListenable: coordinator.settingsNotifier,
          builder: (context, settings, _) {
            final needsCallsign =
                _selectedPlugin.requiresCallsign && !settings.hasValidCallsign;

            return Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              decoration: const BoxDecoration(
                color: Color(0xFF161B22),
                border: Border(
                  top: BorderSide(color: Colors.white12, width: 1),
                ),
              ),
              child: SafeArea(
                top: false,
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // Top strip: Selected protocol badge + attachment indicators
                    Row(
                      children: [
                        // Protocol selector badge
                        InkWell(
                          onTap: isTx ? null : _showProtocolPicker,
                          borderRadius: BorderRadius.circular(6),
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                                horizontal: 8, vertical: 4),
                            decoration: BoxDecoration(
                              color: const Color(0xFF21262D),
                              borderRadius: BorderRadius.circular(6),
                              border: Border.all(
                                color: needsCallsign
                                    ? Colors.amber
                                    : Colors.cyanAccent.withOpacity(0.6),
                                width: 1,
                              ),
                            ),
                            child: Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                Icon(
                                  Icons.tune,
                                  size: 14,
                                  color: needsCallsign
                                      ? Colors.amber
                                      : Colors.cyanAccent,
                                ),
                                const SizedBox(width: 5),
                                Text(
                                  _selectedPlugin.displayName.toUpperCase(),
                                  style: TextStyle(
                                    fontFamily: 'monospace',
                                    fontSize: 11,
                                    fontWeight: FontWeight.bold,
                                    color: needsCallsign
                                        ? Colors.amber
                                        : Colors.cyanAccent,
                                  ),
                                ),
                                const SizedBox(width: 4),
                                const Icon(Icons.arrow_drop_down,
                                    size: 16, color: Colors.white54),
                              ],
                            ),
                          ),
                        ),
                        const SizedBox(width: 8),
                        IconButton(
                          iconSize: 18,
                          padding: EdgeInsets.zero,
                          constraints: const BoxConstraints(),
                          tooltip: "Tweak Modem Parameters",
                          icon: const Icon(Icons.tune, color: Colors.cyanAccent),
                          onPressed: () {
                            showDialog(
                              context: context,
                              builder: (ctx) => const ModemParamsDialog(),
                            );
                          },
                        ),
                        if (needsCallsign) ...[
                          const SizedBox(width: 8),
                          GestureDetector(
                            onTap: () {
                              showDialog(
                                context: context,
                                builder: (ctx) => const StationSettingsDialog(),
                              );
                            },
                            child: Container(
                              padding: const EdgeInsets.symmetric(
                                  horizontal: 6, vertical: 3),
                              decoration: BoxDecoration(
                                color: Colors.amber.withOpacity(0.15),
                                borderRadius: BorderRadius.circular(4),
                                border: Border.all(
                                    color: Colors.amber.withOpacity(0.5), width: 0.8),
                              ),
                              child: Row(
                                children: const [
                                  Icon(Icons.warning_amber_rounded,
                                      size: 12, color: Colors.amber),
                                  SizedBox(width: 4),
                                  Text(
                                    "CALLSIGN REQUIRED",
                                    style: TextStyle(
                                      color: Colors.amber,
                                      fontSize: 9,
                                      fontWeight: FontWeight.bold,
                                    ),
                                  ),
                                ],
                              ),
                            ),
                          ),
                        ],
                        const Spacer(),
                        // HEX / TXT mode toggle
                        InkWell(
                          onTap: isTx
                              ? null
                              : () {
                                  setState(() {
                                    _isHexMode = !_isHexMode;
                                  });
                                },
                          borderRadius: BorderRadius.circular(4),
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                                horizontal: 6, vertical: 3),
                            decoration: BoxDecoration(
                              color: _isHexMode
                                  ? Colors.purpleAccent.withOpacity(0.2)
                                  : const Color(0xFF21262D),
                              borderRadius: BorderRadius.circular(4),
                              border: Border.all(
                                color: _isHexMode
                                    ? Colors.purpleAccent
                                    : Colors.white24,
                                width: 0.8,
                              ),
                            ),
                            child: Text(
                              _isHexMode ? "HEX" : "TXT",
                              style: TextStyle(
                                fontFamily: 'monospace',
                                fontSize: 10,
                                fontWeight: FontWeight.bold,
                                color: _isHexMode
                                    ? Colors.purpleAccent
                                    : Colors.white60,
                              ),
                            ),
                          ),
                        ),
                        const SizedBox(width: 8),
                        // Image attachment preview / button
                        if (_selectedImagePath != null) ...[
                          Container(
                            padding: const EdgeInsets.symmetric(
                                horizontal: 6, vertical: 3),
                            decoration: BoxDecoration(
                              color: Colors.teal.withOpacity(0.2),
                              borderRadius: BorderRadius.circular(4),
                              border: Border.all(color: Colors.tealAccent, width: 0.8),
                            ),
                            child: Row(
                              children: [
                                const Icon(Icons.image,
                                    size: 12, color: Colors.tealAccent),
                                const SizedBox(width: 4),
                                Text(
                                  _selectedImagePath == "synthetic_color_bars.png"
                                      ? "TEST PATTERN"
                                      : p.basename(_selectedImagePath!),
                                  style: const TextStyle(
                                      color: Colors.tealAccent, fontSize: 9),
                                ),
                                const SizedBox(width: 4),
                                GestureDetector(
                                  onTap: () => setState(() {
                                    _selectedImagePath = null;
                                  }),
                                  child: const Icon(Icons.close,
                                      size: 12, color: Colors.white70),
                                ),
                              ],
                            ),
                          ),
                          const SizedBox(width: 6),
                        ],
                        IconButton(
                          icon: Icon(
                            _selectedImagePath != null
                                ? Icons.image
                                : Icons.image_outlined,
                            color: _selectedImagePath != null
                                ? Colors.tealAccent
                                : Colors.white60,
                            size: 20,
                          ),
                          padding: EdgeInsets.zero,
                          constraints: const BoxConstraints(),
                          tooltip: "Attach Image (SSTV/WEFAX)",
                          onPressed: isTx ? null : _showImageOptions,
                        ),
                      ],
                    ),
                    const SizedBox(height: 8),
                    // Input row: TextField + Send Button
                    Row(
                      crossAxisAlignment: CrossAxisAlignment.end,
                      children: [
                        Expanded(
                          child: Container(
                            decoration: BoxDecoration(
                              color: const Color(0xFF0D1117),
                              borderRadius: BorderRadius.circular(8),
                              border: Border.all(
                                color: Colors.white12,
                                width: 1,
                              ),
                            ),
                            child: TextField(
                              controller: _textController,
                              enabled: !isTx,
                              minLines: 1,
                              maxLines: 4,
                              textInputAction: TextInputAction.send,
                              onSubmitted: (_) => _handleTransmit(),
                              style: const TextStyle(
                                color: Colors.white,
                                fontSize: 13,
                                fontFamily: 'monospace',
                              ),
                              decoration: InputDecoration(
                                hintText: _isHexMode
                                    ? "Enter hex bytes (e.g. 48 65 6C 6C 6F or DEADBE)..."
                                    : (_selectedPlugin.category ==
                                            PayloadCategory.image
                                        ? "Add caption or text overlay..."
                                        : "Type message to transmit via ${_selectedPlugin.displayName}..."),
                                hintStyle: const TextStyle(
                                  color: Colors.white30,
                                  fontSize: 12,
                                  fontFamily: 'sans-serif',
                                ),
                                border: InputBorder.none,
                                isDense: true,
                                contentPadding: const EdgeInsets.symmetric(
                                  horizontal: 12,
                                  vertical: 10,
                                ),
                              ),
                            ),
                          ),
                        ),
                        const SizedBox(width: 8),
                        // Transmit Button
                        ElevatedButton(
                          onPressed: isTx ? null : _handleTransmit,
                          style: ElevatedButton.styleFrom(
                            backgroundColor: isTx
                                ? Colors.redAccent
                                : (needsCallsign ? Colors.amber[800] : const Color(0xFF1F6FEB)),
                            foregroundColor: Colors.white,
                            padding: const EdgeInsets.symmetric(
                              horizontal: 16,
                              vertical: 12,
                            ),
                            shape: RoundedRectangleBorder(
                              borderRadius: BorderRadius.circular(8),
                            ),
                            elevation: 2,
                          ),
                          child: isTx
                              ? Row(
                                  mainAxisSize: MainAxisSize.min,
                                  children: const [
                                    SizedBox(
                                      width: 14,
                                      height: 14,
                                      child: CircularProgressIndicator(
                                        strokeWidth: 2,
                                        valueColor: AlwaysStoppedAnimation<Color>(
                                            Colors.white),
                                      ),
                                    ),
                                    SizedBox(width: 6),
                                    Text(
                                      "TX...",
                                      style: TextStyle(
                                        fontSize: 12,
                                        fontWeight: FontWeight.bold,
                                        fontFamily: 'monospace',
                                      ),
                                    ),
                                  ],
                                )
                              : Row(
                                  mainAxisSize: MainAxisSize.min,
                                  children: const [
                                    Icon(Icons.send_rounded, size: 16),
                                    SizedBox(width: 4),
                                    Text(
                                      "TX",
                                      style: TextStyle(
                                        fontSize: 12,
                                        fontWeight: FontWeight.bold,
                                        fontFamily: 'monospace',
                                      ),
                                    ),
                                  ],
                                ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            );
          },
        );
      },
    );
  }
}
