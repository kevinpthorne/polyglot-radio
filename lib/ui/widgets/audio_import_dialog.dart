import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;
import '../../core/modem_coordinator.dart';
import '../../plugins/modem_plugin.dart';

class AudioImportDialog extends StatefulWidget {
  const AudioImportDialog({super.key});

  @override
  State<AudioImportDialog> createState() => _AudioImportDialogState();
}

class _AudioImportDialogState extends State<AudioImportDialog> {
  final TextEditingController _pathController = TextEditingController();
  String _selectedTarget = 'auto';
  bool _isImporting = false;
  String? _errorMessage;

  @override
  void dispose() {
    _pathController.dispose();
    super.dispose();
  }

  Future<void> _pickFile() async {
    try {
      final files = await FilePicker.pickFiles(
        type: FileType.custom,
        allowedExtensions: ['wav', 'pcm', 'raw', 'aiff'],
      );
      if (files.isNotEmpty && files.first.path != null) {
        setState(() {
          _pathController.text = files.first.path!;
          _errorMessage = null;
        });
      }
    } catch (e) {
      setState(() {
        _errorMessage = "File picker error: $e";
      });
    }
  }

  Future<void> _generateSamplePreset(String type) async {
    final sampleRate = 48000;
    final durationSec = 2.0;
    final totalSamples = (sampleRate * durationSec).toInt();
    final pcmData = Int16List(totalSamples);

    double freq1 = 700.0;
    String modemId = 'cw_morse';

    if (type == 'CW') {
      freq1 = 700.0;
      modemId = 'cw_morse';
      for (int i = 0; i < totalSamples; ++i) {
        double t = i / sampleRate;
        double tone = math.sin(2.0 * math.pi * freq1 * t);
        // On-off keying
        double envelope = (i % 8000 < 4000) ? 1.0 : 0.0;
        pcmData[i] = (tone * envelope * 20000).toInt();
      }
    } else if (type == 'APRS') {
      modemId = 'aprs_packet';
      for (int i = 0; i < totalSamples; ++i) {
        double t = i / sampleRate;
        double f = (i % 40 < 20) ? 1200.0 : 2200.0;
        pcmData[i] = (math.sin(2.0 * math.pi * f * t) * 20000).toInt();
      }
    } else if (type == 'Ultrasound') {
      modemId = 'ultrasound';
      for (int i = 0; i < totalSamples; ++i) {
        double t = i / sampleRate;
        double f = 18500.0 + (i % 16) * 80.0;
        pcmData[i] = (math.sin(2.0 * math.pi * f * t) * 20000).toInt();
      }
    }

    // Write standard 44-byte RIFF WAV
    final wavBytes = BytesBuilder();
    // Header
    wavBytes.add(const [0x52, 0x49, 0x46, 0x46]); // "RIFF"
    final fileSize = 36 + totalSamples * 2;
    final b = ByteData(4)..setUint32(0, fileSize, Endian.little);
    wavBytes.add(b.buffer.asUint8List());
    wavBytes.add(const [0x57, 0x41, 0x56, 0x45]); // "WAVE"
    wavBytes.add(const [0x66, 0x6D, 0x74, 0x20]); // "fmt "
    wavBytes.add(const [16, 0, 0, 0]); // Chunk size 16
    wavBytes.add(const [1, 0]); // Audio format 1 (PCM)
    wavBytes.add(const [1, 0]); // 1 Channel (Mono)
    final srData = ByteData(4)..setUint32(0, sampleRate, Endian.little);
    wavBytes.add(srData.buffer.asUint8List());
    final brData = ByteData(4)..setUint32(0, sampleRate * 2, Endian.little);
    wavBytes.add(brData.buffer.asUint8List());
    wavBytes.add(const [2, 0]); // Block align
    wavBytes.add(const [16, 0]); // 16-bit
    wavBytes.add(const [0x64, 0x61, 0x74, 0x61]); // "data"
    final dataSize = ByteData(4)..setUint32(0, totalSamples * 2, Endian.little);
    wavBytes.add(dataSize.buffer.asUint8List());

    final pcmBytes = Uint8List.view(pcmData.buffer);
    wavBytes.add(pcmBytes);

    final tmpPath = p.join(
      ModemCoordinator.instance.storageDirectory,
      'preset_${type.toLowerCase()}_sample.wav',
    );
    final f = File(tmpPath);
    await f.writeAsBytes(wavBytes.toBytes());

    setState(() {
      _pathController.text = tmpPath;
      _selectedTarget = modemId;
      _errorMessage = null;
    });
  }

  Future<void> _handleImport() async {
    final path = _pathController.text.trim();
    if (path.isEmpty) {
      setState(() {
        _errorMessage = "Please select or enter an audio file path.";
      });
      return;
    }

    final file = File(path);
    if (!file.existsSync()) {
      setState(() {
        _errorMessage = "File not found at specified path.";
      });
      return;
    }

    setState(() {
      _isImporting = true;
      _errorMessage = null;
    });

    try {
      final success = await ModemCoordinator.instance.importAudioRecording(
        sourceFilePath: path,
        targetModemId: _selectedTarget,
      );

      if (mounted) {
        Navigator.of(context).pop();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            backgroundColor: const Color(0xFF1F6FEB),
            content: Text(
              success
                  ? "Audio recording imported and queued for decoding."
                  : "Audio recording imported into transmission log.",
            ),
          ),
        );
      }
    } catch (e) {
      if (mounted) {
        setState(() {
          _isImporting = false;
          _errorMessage = "Error importing audio: $e";
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final plugins = PluginRegistry.instance.all;

    return AlertDialog(
      backgroundColor: const Color(0xFF161B22),
      insetPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 20),
      title: Row(
        children: const [
          Icon(Icons.file_upload_outlined, color: Colors.cyanAccent, size: 20),
          SizedBox(width: 8),
          Expanded(
            child: Text(
              "IMPORT AUDIO RECORDING",
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.bold,
                letterSpacing: 1.0,
                fontFamily: 'monospace',
                color: Colors.cyanAccent,
              ),
            ),
          ),
        ],
      ),
      content: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              "Select or enter the path to a pre-recorded WAV audio file to decode via the acoustic modem pipeline.",
              style: TextStyle(color: Colors.white70, fontSize: 12),
            ),
            const SizedBox(height: 14),

            // File selection input row
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _pathController,
                    style: const TextStyle(color: Colors.white, fontSize: 12, fontFamily: 'monospace'),
                    decoration: InputDecoration(
                      hintText: "Path to .wav recording...",
                      hintStyle: const TextStyle(color: Colors.white30, fontSize: 12),
                      errorText: _errorMessage,
                      filled: true,
                      fillColor: Colors.black26,
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(8)),
                      contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                    ),
                    onChanged: (_) {
                      if (_errorMessage != null) {
                        setState(() => _errorMessage = null);
                      }
                    },
                  ),
                ),
                const SizedBox(width: 8),
                ElevatedButton.icon(
                  onPressed: _isImporting ? null : _pickFile,
                  icon: const Icon(Icons.folder_open, size: 16),
                  label: const Text("BROWSE", style: TextStyle(fontSize: 11, fontFamily: 'monospace')),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF21262D),
                    foregroundColor: Colors.cyanAccent,
                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(8),
                      side: BorderSide(color: Colors.cyanAccent.withValues(alpha: 0.5)),
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),

            // Quick Preset Chips
            const Text(
              "Or load a sample waveform:",
              style: TextStyle(color: Colors.white60, fontSize: 11),
            ),
            const SizedBox(height: 6),
            Wrap(
              spacing: 6,
              runSpacing: 4,
              children: [
                _buildPresetChip("CW", "CW Morse"),
                _buildPresetChip("APRS", "APRS Bell 202"),
                _buildPresetChip("Ultrasound", "19kHz Ultrasound"),
              ],
            ),
            const SizedBox(height: 16),

            // Target Decoder Dropdown
            const Text(
              "Target Demodulator",
              style: TextStyle(color: Colors.white70, fontSize: 11, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 6),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 2),
              decoration: BoxDecoration(
                color: Colors.black26,
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: Colors.white24),
              ),
              child: DropdownButtonHideUnderline(
                child: DropdownButton<String>(
                  value: _selectedTarget,
                  isExpanded: true,
                  dropdownColor: const Color(0xFF1E2228),
                  items: [
                    const DropdownMenuItem(
                      value: 'auto',
                      child: Text(
                        "Auto-Detect with Parallel Sentry (All 11 Modems)",
                        style: TextStyle(color: Colors.cyanAccent, fontSize: 12, fontWeight: FontWeight.bold),
                      ),
                    ),
                    ...plugins.map((p) => DropdownMenuItem(
                          value: p.id,
                          child: Text(
                            "${p.displayName} (${p.category.name.toUpperCase()})",
                            style: const TextStyle(color: Colors.white, fontSize: 12),
                          ),
                        )),
                  ],
                  onChanged: (val) {
                    if (val != null) setState(() => _selectedTarget = val);
                  },
                ),
              ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: _isImporting ? null : () => Navigator.of(context).pop(),
          child: const Text("CANCEL", style: TextStyle(color: Colors.white60)),
        ),
        ElevatedButton(
          onPressed: _isImporting ? null : _handleImport,
          style: ElevatedButton.styleFrom(
            backgroundColor: const Color(0xFF1F6FEB),
            foregroundColor: Colors.white,
            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
          ),
          child: _isImporting
              ? const SizedBox(
                  width: 16,
                  height: 16,
                  child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white),
                )
              : const Text("IMPORT & DECODE", style: TextStyle(fontWeight: FontWeight.bold, fontSize: 12)),
        ),
      ],
    );
  }

  Widget _buildPresetChip(String label, String tooltip) {
    return ActionChip(
      label: Text(label, style: const TextStyle(fontSize: 10, fontFamily: 'monospace', color: Colors.cyanAccent)),
      backgroundColor: const Color(0xFF21262D),
      side: BorderSide(color: Colors.cyanAccent.withValues(alpha: 0.3)),
      tooltip: tooltip,
      onPressed: () => _generateSamplePreset(label),
    );
  }
}
