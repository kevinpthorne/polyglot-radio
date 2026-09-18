import 'package:flutter/material.dart';
import '../core/modem_coordinator.dart';
import '../database/models.dart';
import 'composer/transmission_composer.dart';
import 'settings/station_settings_dialog.dart';
import 'widgets/audio_import_dialog.dart';
import 'widgets/status_banner.dart';
import 'widgets/transmission_bubble.dart';
import 'widgets/waterfall_view.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  bool _showWaterfall = true;

  @override
  Widget build(BuildContext context) {
    final coordinator = ModemCoordinator.instance;

    return Scaffold(
      backgroundColor: const Color(0xFF0D1117),
      appBar: AppBar(
        backgroundColor: const Color(0xFF161B22),
        elevation: 0,
        title: Row(
          children: [
            Container(
              padding: const EdgeInsets.all(4),
              decoration: BoxDecoration(
                color: Colors.cyanAccent.withOpacity(0.15),
                borderRadius: BorderRadius.circular(6),
                border: Border.all(color: Colors.cyanAccent.withOpacity(0.5)),
              ),
              child: const Icon(Icons.radar, color: Colors.cyanAccent, size: 20),
            ),
            const SizedBox(width: 10),
            const Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  "Polyglot Radio: Acoustic SDR",
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.bold,
                    letterSpacing: 0.5,
                    color: Colors.white,
                  ),
                ),
                Text(
                  "ACOUSTIC SOFTWARE-DEFINED MODEM",
                  style: TextStyle(
                    fontSize: 9,
                    fontFamily: 'monospace',
                    letterSpacing: 1.0,
                    color: Colors.cyanAccent,
                  ),
                ),
              ],
            ),
          ],
        ),
        actions: [
          // Toggle waterfall view
          IconButton(
            tooltip: _showWaterfall ? "Hide Spectrogram" : "Show Spectrogram",
            icon: Icon(
              _showWaterfall ? Icons.waterfall_chart : Icons.waterfall_chart_outlined,
              color: _showWaterfall ? Colors.cyanAccent : Colors.white54,
              size: 22,
            ),
            onPressed: () {
              setState(() {
                _showWaterfall = !_showWaterfall;
              });
            },
          ),
          // Import pre-saved audio recording button
          IconButton(
            tooltip: "Import Audio Recording",
            icon: const Icon(Icons.file_upload_outlined, color: Colors.cyanAccent, size: 22),
            onPressed: () {
              showDialog(
                context: context,
                builder: (ctx) => const AudioImportDialog(),
              );
            },
          ),
          // Settings button
          IconButton(
            tooltip: "Station Settings",
            icon: const Icon(Icons.settings_outlined, color: Colors.white70, size: 22),
            onPressed: () {
              showDialog(
                context: context,
                builder: (ctx) => const StationSettingsDialog(),
              );
            },
          ),
          const SizedBox(width: 4),
        ],
      ),
      body: Column(
        children: [
          // SDR Sentry Status Banner
          const StatusBanner(),

          // Real-time FFT Waterfall Spectrogram
          if (_showWaterfall)
            const WaterfallView(height: 140),

          // Main Log / Chat Feed
          Expanded(
            child: StreamBuilder<List<Transmission>>(
              stream: coordinator.database.watchTransmissions(),
              builder: (context, snapshot) {
                if (snapshot.hasError) {
                  return Center(
                    child: Text(
                      "Error reading transmissions: ${snapshot.error}",
                      style: const TextStyle(color: Colors.redAccent),
                    ),
                  );
                }

                final transmissions = snapshot.data ?? [];

                if (transmissions.isEmpty) {
                  return Center(
                    child: Padding(
                      padding: const EdgeInsets.all(32),
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(
                            Icons.hearing,
                            size: 48,
                            color: Colors.cyanAccent.withOpacity(0.3),
                          ),
                          const SizedBox(height: 16),
                          const Text(
                            "AWAITING ACOUSTIC SIGNALS",
                            style: TextStyle(
                              color: Colors.white70,
                              fontFamily: 'monospace',
                              fontWeight: FontWeight.bold,
                              letterSpacing: 1.2,
                              fontSize: 13,
                            ),
                          ),
                          const SizedBox(height: 8),
                          const Text(
                            "Parallel Sentry is actively monitoring mic input across all 11 modems.\n"
                            "Transmissions will be promoted and decoded automatically.",
                            textAlign: TextAlign.center,
                            style: TextStyle(
                              color: Colors.white38,
                              fontSize: 12,
                              height: 1.4,
                            ),
                          ),
                        ],
                      ),
                    ),
                  );
                }

                // Show transmissions with newest at bottom
                return ListView.builder(
                  reverse: true,
                  padding: const EdgeInsets.symmetric(vertical: 8),
                  itemCount: transmissions.length,
                  itemBuilder: (context, index) {
                    return TransmissionBubble(
                      transmission: transmissions[index],
                    );
                  },
                );
              },
            ),
          ),

          // Bottom Transmission Composer Bar
          const TransmissionComposer(),
        ],
      ),
    );
  }
}
