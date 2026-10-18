#pragma once
#ifdef __APPLE__
class QWindow;
class QObject;
struct MacOSAccessibilityPreferences {
    bool reduceTransparency = true;
    bool highContrast = false;
};
void performMacOSTitlebarDoubleClick(QWindow *window);
void configureMacOSNativeWindow(QWindow *window);
bool setMacOSBackdrop(QWindow *window, bool enabled);
void setMacOSCornerRadius(QWindow *window, int radius);
MacOSAccessibilityPreferences macOSAccessibilityPreferences();
void observeMacOSAccessibility(QWindow *window, QObject *receiver);
#endif
