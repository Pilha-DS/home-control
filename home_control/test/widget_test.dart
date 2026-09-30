import 'package:flutter_test/flutter_test.dart';
import 'package:home_control/main.dart';

void main() {
  testWidgets('HOME Control carrega o shell', (tester) async {
    await tester.pumpWidget(const HomeControlApp());
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.text('HOME'), findsWidgets);
  });
}
