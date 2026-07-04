package com.openblocks.game;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * NativeActivity subclass whose only job is to make the game truly immersive:
 * hide the status and navigation bars and draw edge-to-edge, so there is no
 * reserved black band above the playfield. The C game code is unchanged and
 * still loaded via the "android.app.lib_name" manifest meta-data.
 *
 * The theme (windowLayoutInDisplayCutoutMode=shortEdges) plus raylib's
 * FLAG_FULLSCREEN_MODE were not enough on Android 11-15 — the system still
 * reserved the status-bar inset. Hiding the system bars from the Activity is
 * the reliable fix. Re-applied on focus gain because the bars can transiently
 * reappear (e.g. after a swipe or returning from background).
 */
public class OpenblocksActivity extends NativeActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        goImmersive();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            goImmersive();
        }
    }

    private void goImmersive() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }
}
