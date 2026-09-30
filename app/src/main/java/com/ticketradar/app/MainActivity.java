package com.ticketradar.app;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity {
    public static final String PREFS = "ticket_radar_prefs";
    public static final String KEY_PRICES = "target_prices";
    public static final String KEY_ARMED = "armed";
    public static final String KEY_AUTO_BUY = "auto_buy_entry";
    public static final String KEY_LAST_SCAN = "last_scan";
    public static final String KEY_LAST_STATUS = "last_status";

    private EditText prices;
    private CheckBox armed;
    private CheckBox autoBuy;
    private TextView status;
    private TextView scanText;
    private final Handler handler = new Handler();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestNotificationsIfNeeded();

        SharedPreferences sp = getSharedPreferences(PREFS, MODE_PRIVATE);

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(24), dp(20), dp(32));
        scroll.addView(root);

        TextView title = new TextView(this);
        title.setText("Ticket Radar · Android");
        title.setTextSize(26);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(title);

        TextView intro = new TextView(this);
        intro.setText("手机独立运行。监控大麦当前前台页面中的正常票档/购票控件；遇到验证码、排队、人机验证、确认订单或支付就停止自动点击。\n\n先在系统里开启 Ticket Radar 的无障碍服务，再打开大麦并进入目标演出页面。");
        intro.setTextSize(15);
        intro.setPadding(0, dp(12), 0, dp(18));
        root.addView(intro);

        addLabel(root, "目标票价（逗号分隔，按优先级）");
        prices = new EditText(this);
        prices.setHint("例如：1580,1280,980,780");
        prices.setText(sp.getString(KEY_PRICES, "1580,1280,980,780"));
        root.addView(prices, matchWrap());

        armed = new CheckBox(this);
        armed.setText("武装监控：发现目标可购票档后自动点击");
        armed.setChecked(sp.getBoolean(KEY_ARMED, false));
        root.addView(armed);

        autoBuy = new CheckBox(this);
        autoBuy.setText("自动点击正常的“立即购买/选座购买”入口");
        autoBuy.setChecked(sp.getBoolean(KEY_AUTO_BUY, true));
        root.addView(autoBuy);

        Button save = button("保存设置");
        save.setOnClickListener(v -> savePrefs());
        root.addView(save, matchWrap());

        Button accessibility = button("① 打开无障碍设置");
        accessibility.setOnClickListener(v -> {
            try {
                startActivity(new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS));
            } catch (Exception e) {
                Toast.makeText(this, "无法打开无障碍设置：" + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        });
        root.addView(accessibility, matchWrap());

        Button damai = button("② 打开大麦 App");
        damai.setOnClickListener(v -> openDamai());
        root.addView(damai, matchWrap());

        Button armNow = button("③ 保存并开始监控");
        armNow.setOnClickListener(v -> {
            armed.setChecked(true);
            savePrefs();
            openDamai();
        });
        root.addView(armNow, matchWrap());

        Button disarm = button("停止自动点击");
        disarm.setOnClickListener(v -> {
            armed.setChecked(false);
            savePrefs();
            Toast.makeText(this, "已停止自动点击", Toast.LENGTH_SHORT).show();
        });
        root.addView(disarm, matchWrap());

        addLabel(root, "当前状态");
        status = new TextView(this);
        status.setTextSize(17);
        status.setTypeface(Typeface.DEFAULT_BOLD);
        status.setPadding(0, dp(8), 0, dp(12));
        root.addView(status);

        addLabel(root, "最近一次从大麦读取到的可访问文字（诊断用）");
        scanText = new TextView(this);
        scanText.setTextSize(13);
        scanText.setTextIsSelectable(true);
        scanText.setPadding(dp(12), dp(12), dp(12), dp(12));
        scanText.setBackgroundColor(0xfff2f2f2);
        root.addView(scanText, new LinearLayout.LayoutParams(-1, dp(260)));

        Button copy = button("复制诊断文字");
        copy.setOnClickListener(v -> {
            android.content.ClipboardManager cm = (android.content.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            cm.setPrimaryClip(android.content.ClipData.newPlainText("Ticket Radar Scan", scanText.getText()));
            Toast.makeText(this, "已复制", Toast.LENGTH_SHORT).show();
        });
        root.addView(copy, matchWrap());

        TextView note = new TextView(this);
        note.setText("说明：本 App 只使用 Android 无障碍提供的正常界面控件执行普通点击，不破解验证码、不绕过排队/风控、不自动确认订单或支付。若大麦某个版本把票档完全画在不可访问的 Canvas/WebView 中，诊断区可能读不到票档文字，需要根据实际页面再适配。");
        note.setTextSize(13);
        note.setPadding(0, dp(20), 0, 0);
        root.addView(note);

        setContentView(scroll);
        refreshUi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        handler.post(refreshLoop);
    }

    @Override
    protected void onPause() {
        super.onPause();
        handler.removeCallbacks(refreshLoop);
    }

    private final Runnable refreshLoop = new Runnable() {
        @Override public void run() {
            refreshUi();
            handler.postDelayed(this, 1000);
        }
    };

    private void savePrefs() {
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putString(KEY_PRICES, prices.getText().toString().trim())
                .putBoolean(KEY_ARMED, armed.isChecked())
                .putBoolean(KEY_AUTO_BUY, autoBuy.isChecked())
                .apply();
        Toast.makeText(this, armed.isChecked() ? "已保存并武装" : "设置已保存", Toast.LENGTH_SHORT).show();
        refreshUi();
    }

    private void refreshUi() {
        if (status == null) return;
        SharedPreferences sp = getSharedPreferences(PREFS, MODE_PRIVATE);
        boolean isArmed = sp.getBoolean(KEY_ARMED, false);
        String st = sp.getString(KEY_LAST_STATUS, "等待大麦页面事件…");
        status.setText((isArmed ? "🟢 已武装\n" : "⚪ 未武装\n") + st);
        scanText.setText(sp.getString(KEY_LAST_SCAN, "还没有读取到大麦页面。\n开启无障碍服务后，打开大麦目标演出页面。"));
    }

    private void openDamai() {
        PackageManager pm = getPackageManager();
        Intent launch = pm.getLaunchIntentForPackage("cn.damai");
        if (launch != null) {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(launch);
        } else {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("market://details?id=cn.damai")));
            } catch (Exception e) {
                Toast.makeText(this, "没有检测到大麦 App", Toast.LENGTH_LONG).show();
            }
        }
    }

    private void requestNotificationsIfNeeded() {
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 1001);
        }
    }

    private void addLabel(LinearLayout root, String text) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextSize(14);
        tv.setTypeface(Typeface.DEFAULT_BOLD);
        tv.setPadding(0, dp(16), 0, dp(6));
        root.addView(tv);
    }

    private Button button(String text) {
        Button b = new Button(this);
        b.setText(text);
        LinearLayout.LayoutParams lp = matchWrap();
        lp.topMargin = dp(8);
        b.setLayoutParams(lp);
        return b;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }
}
