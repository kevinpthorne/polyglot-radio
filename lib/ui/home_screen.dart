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
        titleSpacing: 8,
        title: Row(
          mainAxisSize: MainAxisSize.min,
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
            const SizedBox(width: 8),
            const Flexible(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    "Polyglot Radio: Acoustic SDR",
                    overflow: TextOverflow.ellipsis,
                    maxLines: 1,
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.bold,
                      letterSpacing: 0.3,
                      color: Colors.white,
                    ),
                  ),
                  Text(
                    "ACOUSTIC SOFTWARE-DEFINED MODEM",
                    overflow: TextOverflow.ellipsis,
                    maxLines: 1,
                    style: TextStyle(
                      fontSize: 8,
                      fontFamily: 'monospace',
                      letterSpacing: 0.8,
                      color: Colors.cyanAccent,
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
        actions: [
          // TX Audio Mute Toggle Button
          ValueListenableBuilder<bool>(
            valueListenable: coordinator.isOutputMutedNotifier,
            builder: (context, isMuted, _) {
              return Tooltip(
                message: isMuted
                    ? "TX Audio Muted (Click to Unmute Speaker/TX)"
                    : "TX Audio Active (Click to Mute Speaker/TX)",
                child: InkWell(
                  key: const Key('tx_mute_button'),
                  onTap: () => coordinator.toggleOutputMute(),
                  borderRadius: BorderRadius.circular(6),
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 3),
                    margin: const EdgeInsets.symmetric(vertical: 12, horizontal: 2),
                    decoration: BoxDecoration(
                      color: isMuted
                          ? Colors.redAccent.withOpacity(0.2)
                          : Colors.cyanAccent.withOpacity(0.12),
                      borderRadius: BorderRadius.circular(6),
                      border: Border.all(
                        color: isMuted
                            ? Colors.redAccent.withOpacity(0.7)
                            : Colors.cyanAccent.withOpacity(0.4),
                      ),
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(
                          "TX",
                          style: TextStyle(
                            fontFamily: 'monospace',
                            fontWeight: FontWeight.bold,
                            fontSize: 11,
                            letterSpacing: 0.5,
                            color: isMuted ? Colors.redAccent : Colors.cyanAccent,
                            decoration: isMuted ? TextDecoration.lineThrough : null,
                            decorationColor: Colors.redAccent,
                            decorationThickness: 2,
                          ),
                        ),
                        const SizedBox(width: 2),
                        Icon(
                          isMuted ? Icons.volume_off : Icons.volume_up,
                          size: 12,
                          color: isMuted ? Colors.redAccent : Colors.cyanAccent,
                        ),
                      ],
                    ),
                  ),
                ),
              );
            },
          ),

          // RX Audio Mute Toggle Button
          ValueListenableBuilder<bool>(
            valueListenable: coordinator.isInputMutedNotifier,
            builder: (context, isMuted, _) {
              return Tooltip(
                message: isMuted
                    ? "RX Mic Muted (Click to Unmute Mic/Sentry)"
                    : "RX Mic Active (Click to Mute Mic/Sentry)",
                child: InkWell(
                  key: const Key('rx_mute_button'),
                  onTap: () => coordinator.toggleInputMute(),
                  borderRadius: BorderRadius.circular(6),
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 3),
                    margin: const EdgeInsets.symmetric(vertical: 12, horizontal: 2),
                    decoration: BoxDecoration(
                      color: isMuted
                          ? Colors.redAccent.withOpacity(0.2)
                          : Colors.cyanAccent.withOpacity(0.12),
                      borderRadius: BorderRadius.circular(6),
                      border: Border.all(
                        color: isMuted
                            ? Colors.redAccent.withOpacity(0.7)
                            : Colors.cyanAccent.withOpacity(0.4),
                      ),
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(
                          "RX",
                          style: TextStyle(
                            fontFamily: 'monospace',
                            fontWeight: FontWeight.bold,
                            fontSize: 11,
                            letterSpacing: 0.5,
                            color: isMuted ? Colors.redAccent : Colors.cyanAccent,
                            decoration: isMuted ? TextDecoration.lineThrough : null,
                            decorationColor: Colors.redAccent,
                            decorationThickness: 2,
                          ),
                        ),
                        const SizedBox(width: 2),
                        Icon(
                          isMuted ? Icons.mic_off : Icons.mic,
                          size: 12,
                          color: isMuted ? Colors.redAccent : Colors.cyanAccent,
                        ),
                      ],
                    ),
                  ),
                ),
              );
            },
          ),

          // Toggle waterfall view
          IconButton(
            tooltip: _showWaterfall ? "Hide Spectrogram" : "Show Spectrogram",
            padding: const EdgeInsets.symmetric(horizontal: 4),
            constraints: const BoxConstraints(minWidth: 34, minHeight: 34),
            icon: Icon(
              _showWaterfall ? Icons.waterfall_chart : Icons.waterfall_chart_outlined,
              color: _showWaterfall ? Colors.cyanAccent : Colors.white54,
              size: 20,
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
            padding: const EdgeInsets.symmetric(horizontal: 4),
            constraints: const BoxConstraints(minWidth: 34, minHeight: 34),
            icon: const Icon(Icons.file_upload_outlined, color: Colors.cyanAccent, size: 20),
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
            padding: const EdgeInsets.symmetric(horizontal: 4),
            constraints: const BoxConstraints(minWidth: 34, minHeight: 34),
            icon: const Icon(Icons.settings_outlined, color: Colors.white70, size: 20),
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
            WaterfallView(
              height: MediaQuery.sizeOf(context).height < 700 ? 110 : 140,
            ),

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
                    child: SingleChildScrollView(
                      padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(
                            Icons.hearing,
                            size: 38,
                            color: Colors.cyanAccent.withOpacity(0.3),
                          ),
                          const SizedBox(height: 10),
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
                          const SizedBox(height: 6),
                          const Text(
                            "Parallel Sentry is actively monitoring mic input across all 11 modems.\n"
                            "Transmissions will be promoted and decoded automatically.",
                            textAlign: TextAlign.center,
                            style: TextStyle(
                              color: Colors.white38,
                              fontSize: 11.5,
                              height: 1.35,
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
