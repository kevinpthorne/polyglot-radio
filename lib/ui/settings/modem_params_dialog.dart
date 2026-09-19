import 'package:flutter/material.dart';
import '../../core/modem_coordinator.dart';

class ModemParamsDialog extends StatefulWidget {
  const ModemParamsDialog({super.key});

  @override
  State<ModemParamsDialog> createState() => _ModemParamsDialogState();
}

class _ModemParamsDialogState extends State<ModemParamsDialog> {
  // Rattlegram
  late int _rattlegramCarrier;
  late double _rattlegramSens;
  late String _rattlegramMode;

  // Feld-Hell
  late int _feldHellCarrier;
  late String _feldHellMode;

  // EAS / SAME
  late String _easOriginator;
  late String _easEventCode;

  // CW Morse
  late double _cwPitch;
  late double _cwWpm;

  // SSTV
  late String _sstvMode;

  @override
  void initState() {
    super.initState();
    final settings = ModemCoordinator.instance.settingsNotifier.value;
    _rattlegramCarrier = settings.rattlegramCarrierFreq;
    _rattlegramSens = settings.rattlegramSensitivity;
    _rattlegramMode = settings.rattlegramMode;

    _feldHellCarrier = settings.feldHellCarrierFreq;
    _feldHellMode = settings.feldHellMode;

    _easOriginator = settings.easOriginator;
    _easEventCode = settings.easEventCode;

    _cwPitch = settings.cwPitch;
    _cwWpm = settings.cwWpm;
    _sstvMode = settings.sstvMode;
  }

  void _save() {
    final coordinator = ModemCoordinator.instance;
    final current = coordinator.settingsNotifier.value;
    final updated = current.copyWith(
      rattlegramCarrierFreq: _rattlegramCarrier,
      rattlegramSensitivity: _rattlegramSens,
      rattlegramMode: _rattlegramMode,
      feldHellCarrierFreq: _feldHellCarrier,
      feldHellMode: _feldHellMode,
      easOriginator: _easOriginator,
      easEventCode: _easEventCode,
      cwPitch: _cwPitch,
      cwWpm: _cwWpm,
      sstvMode: _sstvMode,
    );
    coordinator.updateSettings(updated);
    Navigator.of(context).pop(true);
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(
        content: Text('Modem parameters saved & synced to native SDR core.'),
        backgroundColor: Colors.teal,
        duration: Duration(seconds: 2),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Dialog(
      backgroundColor: const Color(0xFF161B22),
      insetPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 20),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: const BorderSide(color: Color(0xFF30363D)),
      ),
      child: Container(
        padding: const EdgeInsets.all(16),
        constraints: const BoxConstraints(maxWidth: 520, maxHeight: 680),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Expanded(
                  child: Row(
                    children: const [
                      Icon(Icons.tune, color: Colors.cyanAccent, size: 20),
                      SizedBox(width: 8),
                      Expanded(
                        child: Text(
                          'Modem Tuning & Parameters',
                          overflow: TextOverflow.ellipsis,
                          style: TextStyle(
                            color: Colors.white,
                            fontSize: 15,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
                IconButton(
                  icon: const Icon(Icons.close, color: Colors.white60, size: 20),
                  onPressed: () => Navigator.of(context).pop(),
                ),
              ],
            ),
            const SizedBox(height: 6),
            const Text(
              'Customize TX carrier frequencies, baud rates, demodulation thresholds, and waveforms. Settings are saved to SQLite and dispatched live to the SDR engine.',
              style: TextStyle(color: Colors.white60, fontSize: 11),
            ),
            const Divider(color: Color(0xFF30363D), height: 20),

            Expanded(
              child: SingleChildScrollView(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    // --- 1. RATTLEGRAM ---
                    _buildSectionHeader(Icons.wifi_tethering, 'Rattlegram (COFDM)'),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('Carrier Freq', style: TextStyle(color: Colors.white70, fontSize: 12)),
                        Text('$_rattlegramCarrier Hz', style: const TextStyle(color: Colors.cyanAccent, fontFamily: 'monospace', fontSize: 12)),
                      ],
                    ),
                    Slider(
                      value: _rattlegramCarrier.toDouble(),
                      min: 1400.0,
                      max: 1900.0,
                      divisions: 25,
                      activeColor: Colors.cyanAccent,
                      onChanged: (val) => setState(() => _rattlegramCarrier = val.round()),
                    ),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                      children: [1500, 1700, 1750].map((preset) {
                        final isSel = _rattlegramCarrier == preset;
                        return OutlinedButton(
                          style: OutlinedButton.styleFrom(
                            foregroundColor: isSel ? Colors.cyanAccent : Colors.white60,
                            side: BorderSide(color: isSel ? Colors.cyanAccent : Colors.white24),
                            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 2),
                          ),
                          onPressed: () => setState(() => _rattlegramCarrier = preset),
                          child: Text('$preset Hz', style: const TextStyle(fontSize: 11)),
                        );
                      }).toList(),
                    ),
                    const SizedBox(height: 8),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('RX Preamble Sensitivity', style: TextStyle(color: Colors.white70, fontSize: 12)),
                        Text('${(_rattlegramSens * 100).toStringAsFixed(0)}%', style: const TextStyle(color: Colors.cyanAccent, fontFamily: 'monospace', fontSize: 12)),
                      ],
                    ),
                    Slider(
                      value: _rattlegramSens,
                      min: 0.25,
                      max: 0.75,
                      divisions: 20,
                      activeColor: Colors.cyanAccent,
                      onChanged: (val) => setState(() => _rattlegramSens = val),
                    ),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('OFDM Transmission Mode', style: TextStyle(color: Colors.white70, fontSize: 12)),
                        DropdownButton<String>(
                          value: _rattlegramMode,
                          dropdownColor: const Color(0xFF21262D),
                          underline: const SizedBox(),
                          items: const [
                            DropdownMenuItem(value: 'mode14', child: Text('Mode 14 (85 B/s - Robust)', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'mode15', child: Text('Mode 15 (128 B/s - Standard)', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'mode16', child: Text('Mode 16 (170 B/s - Fast)', style: TextStyle(color: Colors.white, fontSize: 12))),
                          ],
                          onChanged: (val) {
                            if (val != null) setState(() => _rattlegramMode = val);
                          },
                        ),
                      ],
                    ),

                    const Divider(color: Color(0xFF30363D), height: 24),

                    // --- 2. FELD-HELL ---
                    _buildSectionHeader(Icons.print, 'Feld-Hell (Hellschreiber)'),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('Carrier Freq', style: TextStyle(color: Colors.white70, fontSize: 12)),
                        Text('$_feldHellCarrier Hz', style: const TextStyle(color: Colors.cyanAccent, fontFamily: 'monospace', fontSize: 12)),
                      ],
                    ),
                    Slider(
                      value: _feldHellCarrier.toDouble(),
                      min: 700.0,
                      max: 1800.0,
                      divisions: 44,
                      activeColor: Colors.cyanAccent,
                      onChanged: (val) => setState(() => _feldHellCarrier = val.round()),
                    ),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                      children: [700, 980, 1000, 1225].map((preset) {
                        final isSel = _feldHellCarrier == preset;
                        return OutlinedButton(
                          style: OutlinedButton.styleFrom(
                            foregroundColor: isSel ? Colors.cyanAccent : Colors.white60,
                            side: BorderSide(color: isSel ? Colors.cyanAccent : Colors.white24),
                            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 2),
                          ),
                          onPressed: () => setState(() => _feldHellCarrier = preset),
                          child: Text('$preset Hz', style: const TextStyle(fontSize: 11)),
                        );
                      }).toList(),
                    ),
                    const SizedBox(height: 8),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('Hell Modulation Scheme', style: TextStyle(color: Colors.white70, fontSize: 12)),
                        DropdownButton<String>(
                          value: _feldHellMode,
                          dropdownColor: const Color(0xFF21262D),
                          underline: const SizedBox(),
                          items: const [
                            DropdownMenuItem(value: 'ook', child: Text('Standard OOK Hell (On-Off)', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'fsk240', child: Text('FSK-Hell / FM-Hell (240 Hz shift)', style: TextStyle(color: Colors.white, fontSize: 12))),
                          ],
                          onChanged: (val) {
                            if (val != null) setState(() => _feldHellMode = val);
                          },
                        ),
                      ],
                    ),

                    const Divider(color: Color(0xFF30363D), height: 24),

                    // --- 3. EAS / SAME ---
                    _buildSectionHeader(Icons.warning_amber_rounded, 'EAS / SAME Weather Alert'),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('Alert Originator', style: TextStyle(color: Colors.white70, fontSize: 12)),
                        DropdownButton<String>(
                          value: _easOriginator,
                          dropdownColor: const Color(0xFF21262D),
                          underline: const SizedBox(),
                          items: const [
                            DropdownMenuItem(value: 'EAS', child: Text('EAS - Emergency Action System', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'PEP', child: Text('PEP - Primary Entry Point', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'WXR', child: Text('WXR - National Weather Service', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'CIV', child: Text('CIV - Civil Authorities', style: TextStyle(color: Colors.white, fontSize: 12))),
                          ],
                          onChanged: (val) {
                            if (val != null) setState(() => _easOriginator = val);
                          },
                        ),
                      ],
                    ),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('Event Code', style: TextStyle(color: Colors.white70, fontSize: 12)),
                        DropdownButton<String>(
                          value: _easEventCode,
                          dropdownColor: const Color(0xFF21262D),
                          underline: const SizedBox(),
                          items: const [
                            DropdownMenuItem(value: 'RWT', child: Text('RWT - Required Weekly Test', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'TOR', child: Text('TOR - Tornado Warning', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'SVR', child: Text('SVR - Severe Thunderstorm Warning', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'FFW', child: Text('FFW - Flash Flood Warning', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'EAN', child: Text('EAN - Emergency Action Notification', style: TextStyle(color: Colors.white, fontSize: 12))),
                          ],
                          onChanged: (val) {
                            if (val != null) setState(() => _easEventCode = val);
                          },
                        ),
                      ],
                    ),

                    const Divider(color: Color(0xFF30363D), height: 24),

                    // --- 4. CW MORSE ---
                    _buildSectionHeader(Icons.graphic_eq, 'CW Morse Code'),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('Tone Pitch', style: TextStyle(color: Colors.white70, fontSize: 12)),
                        Text('${_cwPitch.toStringAsFixed(0)} Hz', style: const TextStyle(color: Colors.cyanAccent, fontFamily: 'monospace', fontSize: 12)),
                      ],
                    ),
                    Slider(
                      value: _cwPitch,
                      min: 400.0,
                      max: 1200.0,
                      divisions: 80,
                      activeColor: Colors.cyanAccent,
                      onChanged: (val) => setState(() => _cwPitch = val),
                    ),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('Keying Speed', style: TextStyle(color: Colors.white70, fontSize: 12)),
                        Text('${_cwWpm.toStringAsFixed(0)} WPM', style: const TextStyle(color: Colors.cyanAccent, fontFamily: 'monospace', fontSize: 12)),
                      ],
                    ),
                    Slider(
                      value: _cwWpm,
                      min: 5.0,
                      max: 45.0,
                      divisions: 40,
                      activeColor: Colors.cyanAccent,
                      onChanged: (val) => setState(() => _cwWpm = val),
                    ),

                    const Divider(color: Color(0xFF30363D), height: 24),

                    // --- 5. SSTV ---
                    _buildSectionHeader(Icons.image, 'Slow-Scan TV (SSTV)'),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('Default TX Mode', style: TextStyle(color: Colors.white70, fontSize: 12)),
                        DropdownButton<String>(
                          value: _sstvMode,
                          dropdownColor: const Color(0xFF21262D),
                          underline: const SizedBox(),
                          items: const [
                            DropdownMenuItem(value: 'robot36', child: Text('Robot 36 (Color, 36s)', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'robot72', child: Text('Robot 72 (Color, 72s)', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'martin1', child: Text('Martin 1 (RGB, 114s)', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'martin2', child: Text('Martin 2 (RGB, 58s)', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'scottie1', child: Text('Scottie 1 (RGB, 110s)', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'scottie2', child: Text('Scottie 2 (RGB, 71s)', style: TextStyle(color: Colors.white, fontSize: 12))),
                            DropdownMenuItem(value: 'pd120', child: Text('PD-120 (High-Res, 120s)', style: TextStyle(color: Colors.white, fontSize: 12))),
                          ],
                          onChanged: (val) {
                            if (val != null) setState(() => _sstvMode = val);
                          },
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),

            const SizedBox(height: 12),
            Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: [
                TextButton(
                  onPressed: () => Navigator.of(context).pop(),
                  child: const Text('Cancel', style: TextStyle(color: Colors.white60)),
                ),
                const SizedBox(width: 8),
                ElevatedButton.icon(
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.teal,
                    foregroundColor: Colors.white,
                  ),
                  onPressed: _save,
                  icon: const Icon(Icons.check, size: 16),
                  label: const Text('Save Parameters'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildSectionHeader(IconData icon, String title) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8, top: 4),
      child: Row(
        children: [
          Icon(icon, size: 14, color: Colors.cyanAccent),
          const SizedBox(width: 6),
          Text(
            title,
            style: const TextStyle(
              color: Colors.cyanAccent,
              fontSize: 13,
              fontWeight: FontWeight.bold,
              letterSpacing: 0.5,
            ),
          ),
        ],
      ),
    );
  }
}
