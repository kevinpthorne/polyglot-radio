import 'package:flutter/material.dart';
import '../../plugins/modem_plugin.dart';

class ProtocolPickerSheet extends StatelessWidget {
  final ModemProtocolPlugin currentSelection;
  final bool hasImage;
  final bool hasText;
  final ValueChanged<ModemProtocolPlugin> onSelected;

  const ProtocolPickerSheet({
    super.key,
    required this.currentSelection,
    required this.hasImage,
    required this.hasText,
    required this.onSelected,
  });

  @override
  Widget build(BuildContext context) {
    final available = PluginRegistry.instance.getSelectableProtocols(
      hasImage: hasImage,
      hasText: hasText,
    );

    return Container(
      padding: const EdgeInsets.symmetric(vertical: 16),
      decoration: const BoxDecoration(
        color: Color(0xFF161B22),
        borderRadius: BorderRadius.vertical(top: Radius.circular(16)),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
            child: Row(
              children: const [
                Icon(Icons.tune, color: Colors.cyanAccent, size: 18),
                SizedBox(width: 8),
                Text(
                  "SELECT TRANSMIT MODEM",
                  style: TextStyle(
                    color: Colors.cyanAccent,
                    fontSize: 12,
                    fontWeight: FontWeight.bold,
                    letterSpacing: 1.0,
                    fontFamily: 'monospace',
                  ),
                ),
              ],
            ),
          ),
          const Divider(color: Colors.white12),
          Flexible(
            child: ListView.builder(
              shrinkWrap: true,
              itemCount: available.length,
              itemBuilder: (context, index) {
                final plugin = available[index];
                final isSelected = plugin.id == currentSelection.id;

                return ListTile(
                  dense: true,
                  leading: _buildCategoryIcon(plugin.category),
                  title: Row(
                    children: [
                      Text(
                        plugin.displayName,
                        style: TextStyle(
                          color: isSelected ? Colors.cyanAccent : Colors.white,
                          fontWeight: isSelected ? FontWeight.bold : FontWeight.normal,
                        ),
                      ),
                      if (plugin.requiresCallsign) ...[
                        const SizedBox(width: 8),
                        Container(
                          padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
                          decoration: BoxDecoration(
                            color: Colors.amber.withOpacity(0.2),
                            borderRadius: BorderRadius.circular(4),
                            border: Border.all(color: Colors.amber, width: 0.6),
                          ),
                          child: const Text(
                            "HAM CALL REQ",
                            style: TextStyle(color: Colors.amber, fontSize: 9, fontWeight: FontWeight.bold),
                          ),
                        ),
                      ],
                    ],
                  ),
                  subtitle: Text(
                    plugin.description,
                    style: const TextStyle(color: Colors.white60, fontSize: 11),
                  ),
                  trailing: isSelected
                      ? const Icon(Icons.check_circle, color: Colors.cyanAccent, size: 20)
                      : null,
                  onTap: () {
                    onSelected(plugin);
                    Navigator.pop(context);
                  },
                );
              },
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildCategoryIcon(PayloadCategory cat) {
    switch (cat) {
      case PayloadCategory.image:
        return const Icon(Icons.image, color: Colors.tealAccent, size: 20);
      case PayloadCategory.packet:
        return const Icon(Icons.layers, color: Colors.lightBlueAccent, size: 20);
      case PayloadCategory.textStream:
        return const Icon(Icons.terminal, color: Colors.amberAccent, size: 20);
      case PayloadCategory.inaudible:
        return const Icon(Icons.graphic_eq, color: Colors.purpleAccent, size: 20);
    }
  }
}
