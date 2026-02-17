package com.dxxrebirth.d1x;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import org.libsdl.app.SDLActivity;

/**
 * SDLActivity subclass that requests MANAGE_EXTERNAL_STORAGE permission
 * on Android 11+ so users can place game data files in /sdcard/dxx-rebirth/
 * without needing adb root.
 */
public class DxxActivity extends SDLActivity {
    private static final String TAG = "DxxActivity";
    private static final int REQUEST_MANAGE_STORAGE = 1001;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestStoragePermissionIfNeeded();
    }

    private void requestStoragePermissionIfNeeded() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                Log.i(TAG, "Requesting MANAGE_EXTERNAL_STORAGE permission");
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.setData(Uri.parse("package:" + getPackageName()));
                    startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
                } catch (Exception e) {
                    // Fallback for devices that don't support the per-app intent
                    Log.w(TAG, "Per-app intent failed, trying global intent", e);
                    Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
                }
            } else {
                Log.i(TAG, "MANAGE_EXTERNAL_STORAGE already granted");
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MANAGE_STORAGE) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                if (Environment.isExternalStorageManager()) {
                    Log.i(TAG, "MANAGE_EXTERNAL_STORAGE granted");
                } else {
                    Log.w(TAG, "MANAGE_EXTERNAL_STORAGE denied - /sdcard/dxx-rebirth/ won't be accessible");
                }
            }
        }
    }
}
