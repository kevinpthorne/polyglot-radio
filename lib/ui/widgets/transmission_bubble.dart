import 'dart:io';
import 'dart:ui' as ui;
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;
import '../../database/models.dart';
import '../../plugins/modem_plugin.dart';
import '../../core/modem_coordinator.dart';
import 'audio_player_bar.dart';

class TransmissionBubble extends StatelessWidget {
  final Transmission transmission;

  const TransmissionBubble({super.key, required this.transmission});

  @override
  Widget build(BuildContext context) {
    final isTx = transmission.direction == TransmissionDirection.tx;

    return Align(
      alignment: isTx ? Alignment.centerRight : Alignment.centerLeft,
      child: Container(
        margin: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
        constraints: BoxConstraints(
          maxWidth: (MediaQuery.of(context).size.width * 0.90).clamp(280.0, 560.0),
        ),
        decoration: BoxDecoration(
          color: isTx ? const Color(0xFF1F2E40) : const Color(0xFF1E2228),
          borderRadius: BorderRadius.only(
            topLeft: const Radius.circular(12),
            topRight: const Radius.circular(12),
            bottomLeft: Radius.circular(isTx ? 12 : 2),
            bottomRight: Radius.circular(isTx ? 2 : 12),
          ),
          border: Border.all(
            color: isTx
                ? Colors.blueAccent.withOpacity(0.4)
                : (transmission.isIdentified
                    ? Colors.white12
                    : Colors.amber.withOpacity(0.4)),
            width: 1.0,
          ),
          boxShadow: [
            BoxShadow(
              color: Colors.black.withOpacity(0.2),
              blurRadius: 4,
              offset: const Offset(0, 2),
            ),
          ],
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Header: Protocol Badge, Direction, SNR, Time
            _buildHeader(context, isTx),
            // Body: Content tailored to payload type
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              child: _buildBody(context),
            ),
            // Embedded Audio Player Bar
            if (transmission.audioFilePath.isNotEmpty &&
                transmission.audioFilePath != 'outbound_tx')
              Padding(
                padding: const EdgeInsets.fromLTRB(12, 0, 12, 10),
                child: AudioPlayerBar(
                  audioFilePath: transmission.audioFilePath,
                  durationMs: transmission.durationMs,
                ),
              ),
          ],
        ),
      ),
    );
  }

  Widget _buildHeader(BuildContext context, bool isTx) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: Colors.black.withOpacity(0.2),
        borderRadius: const BorderRadius.only(
          topLeft: Radius.circular(11),
          topRight: Radius.circular(11),
        ),
      ),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Flexible(
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(
                  isTx ? Icons.arrow_upward : Icons.arrow_downward,
                  size: 13,
                  color: isTx ? Colors.lightBlueAccent : Colors.cyanAccent,
                ),
                const SizedBox(width: 5),
                Flexible(
                  child: Text(
                    transmission.protocolDisplayName.toUpperCase(),
                    overflow: TextOverflow.ellipsis,
                    maxLines: 1,
                    style: TextStyle(
                      fontSize: 10.5,
                      fontWeight: FontWeight.bold,
                      color: isTx ? Colors.lightBlueAccent : Colors.cyanAccent,
                      fontFamily: 'monospace',
                      letterSpacing: 0.4,
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 6),
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (transmission.snrDb != null) ...[
                Text(
                  "${transmission.snrDb!.toStringAsFixed(1)} dB",
                  style: const TextStyle(
                    fontSize: 9.5,
                    color: Colors.white60,
                    fontFamily: 'monospace',
                  ),
                ),
                const SizedBox(width: 6),
              ],
              Text(
                _formatTimestamp(transmission.timestamp),
                style: const TextStyle(fontSize: 9.5, color: Colors.white38),
              ),
              if (!isTx &&
                  transmission.audioFilePath.isNotEmpty &&
                  transmission.audioFilePath != 'outbound_tx') ...[
                const SizedBox(width: 4),
                PopupMenuButton<String>(
                  tooltip: "Re-classify / Re-demodulate as other modem",
                  icon: const Icon(Icons.alt_route_rounded, size: 15, color: Colors.amberAccent),
                  padding: EdgeInsets.zero,
                  constraints: const BoxConstraints(),
                  color: const Color(0xFF1E293B),
              itemBuilder: (ctx) {
                return PluginRegistry.instance.all.map((p) {
                  return PopupMenuItem<String>(
                    value: p.id,
                    height: 32,
                    child: Row(
                      children: [
                        Icon(
                          p.category == PayloadCategory.image
                              ? Icons.image_outlined
                              : p.category == PayloadCategory.packet
                                  ? Icons.all_inbox_outlined
                                  : Icons.text_snippet_outlined,
                          size: 15,
                          color: p.id == transmission.protocolId
                              ? Colors.amberAccent
                              : Colors.white70,
                        ),
                        const SizedBox(width: 8),
                        Text(
                          p.displayName,
                          style: TextStyle(
                            fontSize: 12,
                            color: p.id == transmission.protocolId
                                ? Colors.amberAccent
                                : Colors.white,
                            fontWeight: p.id == transmission.protocolId
                                ? FontWeight.bold
                                : FontWeight.normal,
                          ),
                        ),
                      ],
                    ),
                  );
                }).toList();
              },
              onSelected: (targetModemId) {
                final success = ModemCoordinator.instance.reprocessRecording(
                  wavPath: transmission.audioFilePath,
                  targetModemId: targetModemId,
                );
                final targetPlugin = PluginRegistry.instance.get(targetModemId);
                final name = targetPlugin?.displayName ?? targetModemId;
                ScaffoldMessenger.of(context).showSnackBar(
                  SnackBar(
                    backgroundColor: const Color(0xFF1E293B),
                    content: Text(
                      success
                          ? "Re-demodulating recording as $name..."
                          : "Failed to start re-demodulation as $name",
                      style: const TextStyle(color: Colors.cyanAccent),
                    ),
                    duration: const Duration(seconds: 3),
                  ),
                );
              },
            ),
          ],
        ],
      ),
    ],
  ),
);
}

  Widget _buildBody(BuildContext context) {
    if (!transmission.isIdentified || transmission.payloadType == PayloadType.unknown) {
      return _buildUnknownBurst(context);
    }

    switch (transmission.payloadType) {
      case PayloadType.image:
        return _buildImageBubble(context);
      case PayloadType.packet:
        return _buildPacketBubble(context);
      case PayloadType.text:
      default:
        return _buildTextStreamBubble(context);
    }
  }

  Widget _buildUnknownBurst(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: const [
            Icon(Icons.warning_amber_rounded, size: 16, color: Colors.amber),
            SizedBox(width: 6),
            Text(
              "Unidentified Acoustic Burst",
              style: TextStyle(
                color: Colors.amber,
                fontSize: 12,
                fontWeight: FontWeight.bold,
              ),
            ),
          ],
        ),
        const SizedBox(height: 6),
        const Text(
          "Carrier detected but preamble confidence was below auto-promotion threshold.",
          style: TextStyle(color: Colors.white70, fontSize: 11),
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            const Text("Re-demodulate as:", style: TextStyle(color: Colors.white60, fontSize: 11)),
            const SizedBox(width: 8),
            DropdownButton<String>(
              isDense: true,
              dropdownColor: const Color(0xFF21262D),
              hint: const Text("Select mode", style: TextStyle(color: Colors.cyanAccent, fontSize: 11)),
              underline: const SizedBox(),
              items: PluginRegistry.instance.all.map((plugin) {
                return DropdownMenuItem(
                  value: plugin.id,
                  child: Text(
                    plugin.displayName,
                    style: const TextStyle(color: Colors.white, fontSize: 11),
                  ),
                );
              }).toList(),
              onChanged: (selectedModemId) {
                if (selectedModemId != null) {
                  ModemCoordinator.instance.reprocessRecording(
                    wavPath: transmission.audioFilePath,
                    targetModemId: selectedModemId,
                  );
                }
              },
            ),
          ],
        ),
      ],
    );
  }

  Widget _buildImageBubble(BuildContext context) {
    final imagePath = transmission.imageFilePath;
    final file = imagePath != null ? File(imagePath) : null;
    final hasImageFile = file != null && file.existsSync();
    final isActiveBurst = ModemCoordinator.instance.isSentryLocked &&
        (transmission.id == ModemCoordinator.instance.activeImageTxId);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if (hasImageFile)
          ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: Image.file(
              file,
              fit: BoxFit.contain,
              width: double.infinity,
              height: 180,
            ),
          )
        else if (isActiveBurst)
          ValueListenableBuilder<ui.Image?>(
            valueListenable: ModemCoordinator.instance.liveCanvasNotifier,
            builder: (context, liveImage, child) {
              if (liveImage != null) {
                return ClipRRect(
                  borderRadius: BorderRadius.circular(8),
                  child: RawImage(
                    image: liveImage,
                    fit: BoxFit.contain,
                    width: double.infinity,
                    height: 180,
                  ),
                );
              }
              return _buildPlaceholderContainer();
            },
          )
        else
          _buildPlaceholderContainer(),
        const SizedBox(height: 6),
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(
              "Format: ${transmission.protocolDisplayName}",
              style: const TextStyle(color: Colors.white60, fontSize: 10, fontFamily: 'monospace'),
            ),
            if (hasImageFile)
              IconButton(
                iconSize: 18,
                padding: EdgeInsets.zero,
                constraints: const BoxConstraints(),
                tooltip: "Export Image (.png)",
                icon: const Icon(Icons.file_download_outlined, color: Colors.cyanAccent),
                onPressed: () => _exportImage(context, file),
              )
            else
              Text(
                isActiveBurst ? "Sync PLL: Receiving..." : "Frame Complete",
                style: TextStyle(
                  color: isActiveBurst ? Colors.greenAccent : Colors.white38,
                  fontSize: 10,
                  fontFamily: 'monospace',
                ),
              ),
          ],
        ),
      ],
    );
  }

  Widget _buildPlaceholderContainer() {
    return Container(
      height: 120,
      width: double.infinity,
      decoration: BoxDecoration(
        color: Colors.black45,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.white12),
      ),
      child: const Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.broken_image, color: Colors.white38, size: 28),
            SizedBox(height: 4),
            Text("Scanline Frame Buffer", style: TextStyle(color: Colors.white38, fontSize: 10)),
          ],
        ),
      ),
    );
  }

  Future<void> _exportImage(BuildContext context, File file) async {
    final suggestedName = p.basename(file.path);
    try {
      final bytes = await file.readAsBytes();
      final resultUri = await FilePicker.saveFile(
        dialogTitle: 'Export Image (.png)',
        fileName: suggestedName.isNotEmpty ? suggestedName : 'sstv_image.png',
        bytes: bytes,
        type: FileType.custom,
        allowedExtensions: ['png'],
      );
      if (resultUri != null && context.mounted) {
        final displayName = p.basename(resultUri.path);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Image exported to $displayName')),
        );
      }
    } catch (e) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Export failed: $e')),
        );
      }
    }
  }

  Widget _buildTextStreamBubble(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(8),
      decoration: BoxDecoration(
        color: Colors.black.withOpacity(0.3),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: Colors.white10),
      ),
      child: SelectableText(
        transmission.textContent ?? '(Empty stream)',
        style: const TextStyle(
          color: Colors.white,
          fontSize: 13,
          fontFamily: 'monospace',
          letterSpacing: 0.5,
        ),
      ),
    );
  }

  Widget _buildPacketBubble(BuildContext context) {
    final isAprs = transmission.protocolId == 'aprs_packet';
    final isEas = transmission.protocolId == 'eas_same';

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if (isAprs)
          _buildAprsCard(context)
        else if (isEas)
          _buildEasCard(context)
        else
          _buildTextStreamBubble(context),
      ],
    );
  }

  Widget _buildAprsCard(BuildContext context) {
    final text = transmission.textContent ?? '';
    return Container(
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: Colors.black.withOpacity(0.35),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.lightBlueAccent.withOpacity(0.3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: const [
              Icon(Icons.location_on, size: 14, color: Colors.lightBlueAccent),
              SizedBox(width: 4),
              Text(
                "APRS AX.25 UI Frame",
                style: TextStyle(
                  color: Colors.lightBlueAccent,
                  fontWeight: FontWeight.bold,
                  fontSize: 11,
                  fontFamily: 'monospace',
                ),
              ),
            ],
          ),
          const SizedBox(height: 6),
          SelectableText(
            text,
            style: const TextStyle(
              color: Colors.white,
              fontSize: 12,
              fontFamily: 'monospace',
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildEasCard(BuildContext context) {
    final text = transmission.textContent ?? '';
    return Container(
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: Colors.red.withOpacity(0.12),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.redAccent.withOpacity(0.4)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: const [
              Icon(Icons.campaign, size: 16, color: Colors.redAccent),
              SizedBox(width: 6),
              Text(
                "EAS EMERGENCY ALERT",
                style: TextStyle(
                  color: Colors.redAccent,
                  fontWeight: FontWeight.bold,
                  fontSize: 11,
                  fontFamily: 'monospace',
                ),
              ),
            ],
          ),
          const SizedBox(height: 6),
          SelectableText(
            text,
            style: const TextStyle(
              color: Colors.white,
              fontSize: 12,
              fontFamily: 'monospace',
            ),
          ),
        ],
      ),
    );
  }

  String _formatTimestamp(int ms) {
    final date = DateTime.fromMillisecondsSinceEpoch(ms);
    final hour = date.hour.toString().padLeft(2, '0');
    final min = date.minute.toString().padLeft(2, '0');
    final sec = date.second.toString().padLeft(2, '0');
    return '$hour:$min:$sec';
  }
}
