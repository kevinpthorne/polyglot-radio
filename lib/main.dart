import 'package:flutter/material.dart';
import 'core/modem_coordinator.dart';
import 'ui/home_screen.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Initialize Native C++ Core and Audio HAL
  try {
    await ModemCoordinator.instance.initialize(enableLoopback: false);
  } catch (e) {
    debugPrint("Failed to initialize ModemCoordinator on startup: $e");
  }

  runApp(const PolyglotRadioApp());
}

class PolyglotRadioApp extends StatelessWidget {
  const PolyglotRadioApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Polyglot Radio: Acoustic SDR',
      debugShowCheckedModeBanner: false,
      themeMode: ThemeMode.dark,
      darkTheme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: const Color(0xFF0D1117),
        primaryColor: const Color(0xFF1F6FEB),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF1F6FEB),
          secondary: Colors.cyanAccent,
          surface: Color(0xFF161B22),
          error: Colors.redAccent,
        ),
        appBarTheme: const AppBarTheme(
          backgroundColor: Color(0xFF161B22),
          elevation: 0,
          titleTextStyle: TextStyle(
            color: Colors.white,
            fontSize: 16,
            fontWeight: FontWeight.bold,
          ),
        ),
        dividerColor: Colors.white12,
        fontFamily: 'sans-serif',
      ),
      home: const HomeScreen(),
    );
  }
}
