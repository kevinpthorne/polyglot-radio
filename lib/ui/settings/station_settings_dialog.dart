import 'dart:convert';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;
import '../../core/modem_coordinator.dart';
import 'modem_params_dialog.dart';

class StationSettingsDialog extends StatefulWidget {
  const StationSettingsDialog({super.key});

  @override
  State<StationSettingsDialog> createState() => _StationSettingsDialogState();
}

class _StationSettingsDialogState extends State<StationSettingsDialog> {
  late final TextEditingController _callsignCtrl;
  late final TextEditingController _symbolCtrl;
  late final TextEditingController _fipsCtrl;
  late double _squelchDb;
  late bool _isLoopback;
  String? _callsignError;

  @override
  void initState() {
    super.initState();
    final settings = ModemCoordinator.instance.settingsNotifier.value;
    _callsignCtrl = TextEditingController(text: settings.callsign ?? '');
    _symbolCtrl = TextEditingController(text: settings.stationSymbol);
    _fipsCtrl = TextEditingController(text: settings.fipsCountyCode);
    _squelchDb = settings.squelchThresholdDb;
    _isLoopback = settings.isLoopbackEnabled;
  }

  @override
  void dispose() {
    _callsignCtrl.dispose();
    _symbolCtrl.dispose();
    _fipsCtrl.dispose();
    super.dispose();
  }

  Future<void> _dumpDatabase() async {
    try {
      final coordinator = ModemCoordinator.instance;
      final txs = coordinator.database.getTransmissions(limit: 10000);
      final jsonList = txs.map((t) => t.toMap()).toList();
      final jsonStr = const JsonEncoder.withIndent('  ').convert(jsonList);

      final now = DateTime.now().millisecondsSinceEpoch;
      final dir = Directory(coordinator.storageDirectory);
      if (!dir.existsSync()) dir.createSync(recursive: true);
      final filePath = p.join(dir.path, 'polyglot_database_dump_$now.json');
      final file = File(filePath);
      await file.writeAsString(jsonStr);

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text("Database dumped (${txs.length} records) to:\n$filePath"),
            duration: const Duration(seconds: 5),
            backgroundColor: Colors.teal,
          ),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text("Error dumping database: $e"), backgroundColor: Colors.redAccent),
        );
      }
    }
  }

  Future<void> _clearDatabase() async {
    final confirm = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: const Color(0xFF161B22),
        title: const Text("PURGE ALL TRANSMISSIONS", style: TextStyle(color: Colors.redAccent, fontSize: 14)),
        content: const Text(
          "Are you sure you want to delete all saved transmissions from the database? This action cannot be undone.",
          style: TextStyle(color: Colors.white70, fontSize: 12),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text("CANCEL", style: TextStyle(color: Colors.white54)),
          ),
          ElevatedButton(
            style: ElevatedButton.styleFrom(backgroundColor: Colors.redAccent),
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text("PURGE ALL"),
          ),
        ],
      ),
    );

    if (confirm == true) {
      ModemCoordinator.instance.database.clearAll();
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text("All transmissions purged from database."), backgroundColor: Colors.redAccent),
        );
      }
    }
  }

  void _saveSettings() {
    final text = _callsignCtrl.text.trim().toUpperCase();
    if (text.isNotEmpty) {
      final regex = RegExp(r'^[A-Z0-9]{1,3}\d[A-Z]{1,4}(-\d{1,2})?$');
      if (!regex.hasMatch(text)) {
        setState(() {
          _callsignError = "Invalid legal callsign format (e.g. W1AW, K6OTA-7)";
        });
        return;
      }
    }

    final current = ModemCoordinator.instance.settingsNotifier.value;
    final newSettings = current.copyWith(
      callsign: text.isEmpty ? null : text,
      clearCallsign: text.isEmpty,
      stationSymbol: _symbolCtrl.text.trim().isEmpty ? "/-" : _symbolCtrl.text.trim(),
      fipsCountyCode: _fipsCtrl.text.trim().isEmpty ? "000000" : _fipsCtrl.text.trim(),
      squelchThresholdDb: _squelchDb,
      isLoopbackEnabled: _isLoopback,
    );

    ModemCoordinator.instance.updateSettings(newSettings);
    Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      backgroundColor: const Color(0xFF161B22),
      title: Row(
        children: const [
          Icon(Icons.settings, color: Colors.cyanAccent, size: 20),
          SizedBox(width: 8),
          Text(
            "STATION SETTINGS",
            style: TextStyle(
              fontSize: 14,
              fontWeight: FontWeight.bold,
              letterSpacing: 1.0,
              fontFamily: 'monospace',
              color: Colors.cyanAccent,
            ),
          ),
        ],
      ),
      content: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Callsign configuration (Legal amateur radio compliance)
            const Text(
              "Amateur Radio Callsign (Required for APRS)",
              style: TextStyle(color: Colors.white70, fontSize: 11),
            ),
            const SizedBox(height: 4),
            TextField(
              controller: _callsignCtrl,
              textCapitalization: TextCapitalization.characters,
              style: const TextStyle(color: Colors.white, fontFamily: 'monospace'),
              decoration: InputDecoration(
                hintText: "e.g. W1AW, K6OTA-7",
                hintStyle: const TextStyle(color: Colors.white30, fontSize: 12),
                errorText: _callsignError,
                filled: true,
                fillColor: Colors.black26,
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(8)),
                contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              ),
              onChanged: (_) {
                if (_callsignError != null) {
                  setState(() => _callsignError = null);
                }
              },
            ),
            const SizedBox(height: 14),

            // Station Symbol
            const Text(
              "APRS Station Symbol",
              style: TextStyle(color: Colors.white70, fontSize: 11),
            ),
            const SizedBox(height: 4),
            TextField(
              controller: _symbolCtrl,
              style: const TextStyle(color: Colors.white, fontFamily: 'monospace'),
              decoration: InputDecoration(
                filled: true,
                fillColor: Colors.black26,
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(8)),
                contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              ),
            ),
            const SizedBox(height: 14),

            // Squelch sensitivity
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text("Rx Squelch Threshold", style: TextStyle(color: Colors.white70, fontSize: 11)),
                Text("${_squelchDb.toStringAsFixed(0)} dBFS",
                    style: const TextStyle(color: Colors.cyanAccent, fontSize: 11, fontFamily: 'monospace')),
              ],
            ),
            Slider(
              value: _squelchDb,
              min: -80.0,
              max: -20.0,
              divisions: 60,
              activeColor: Colors.cyanAccent,
              onChanged: (val) => setState(() => _squelchDb = val),
            ),
            const SizedBox(height: 8),

            // Virtual Loopback mode switch
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text(
                "Virtual Audio Loopback (CI / Testing)",
                style: TextStyle(color: Colors.white, fontSize: 12),
              ),
              subtitle: const Text(
                "Directly bridges transmitter to receiver without physical speaker/mic",
                style: TextStyle(color: Colors.white54, fontSize: 10),
              ),
              value: _isLoopback,
              activeThumbColor: Colors.purpleAccent,
              onChanged: (val) => setState(() => _isLoopback = val),
            ),
            const Divider(color: Colors.white12, height: 24),

            // Modem Parameters Tuning
            OutlinedButton.icon(
              style: OutlinedButton.styleFrom(
                foregroundColor: Colors.cyanAccent,
                side: const BorderSide(color: Colors.cyanAccent),
                minimumSize: const Size(double.infinity, 38),
              ),
              icon: const Icon(Icons.tune, size: 16),
              label: const Text("TWEAK MODEM PARAMETERS (CARRIER, CW, SSTV)",
                  style: TextStyle(fontSize: 11, fontWeight: FontWeight.bold)),
              onPressed: () {
                showDialog(
                  context: context,
                  builder: (ctx) => const ModemParamsDialog(),
                );
              },
            ),
            const Divider(color: Colors.white12, height: 24),

            // Database Management Section
            const Text(
              "DATABASE MANAGEMENT",
              style: TextStyle(color: Colors.cyanAccent, fontSize: 11, fontWeight: FontWeight.bold, fontFamily: 'monospace'),
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: OutlinedButton.icon(
                    style: OutlinedButton.styleFrom(
                      foregroundColor: Colors.cyanAccent,
                      side: const BorderSide(color: Colors.cyanAccent),
                    ),
                    icon: const Icon(Icons.file_download_outlined, size: 16),
                    label: const Text("DUMP JSON", style: TextStyle(fontSize: 11)),
                    onPressed: _dumpDatabase,
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: OutlinedButton.icon(
                    style: OutlinedButton.styleFrom(
                      foregroundColor: Colors.redAccent,
                      side: const BorderSide(color: Colors.redAccent),
                    ),
                    icon: const Icon(Icons.delete_forever_outlined, size: 16),
                    label: const Text("PURGE ALL", style: TextStyle(fontSize: 11)),
                    onPressed: _clearDatabase,
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text("CANCEL", style: TextStyle(color: Colors.white54)),
        ),
        ElevatedButton(
          style: ElevatedButton.styleFrom(
            backgroundColor: Colors.cyanAccent.withValues(alpha: 0.2),
            foregroundColor: Colors.cyanAccent,
          ),
          onPressed: _saveSettings,
          child: const Text("SAVE"),
        ),
      ],
    );
  }
}
