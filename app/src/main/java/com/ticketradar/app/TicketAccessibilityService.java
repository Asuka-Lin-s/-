package com.ticketradar.app;

import android.accessibilityservice.AccessibilityService;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.SystemClock;
import android.text.TextUtils;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class TicketAccessibilityService extends AccessibilityService {
    private static final String DAMAI_PACKAGE = "cn.damai";
    private static final String CHANNEL = "ticket_radar_alerts";
    private static final long ACTION_COOLDOWN_MS = 1800;
    private static final long DUPLICATE_TRIGGER_MS = 15000;

    private static final List<String> BLOCKERS = Arrays.asList(
            "验证码", "滑块", "安全验证", "人机验证", "请完成验证", "排队中", "正在排队",
            "确认订单", "提交订单", "收银台", "支付订单", "立即支付", "支付"
    );
    private static final List<String> NEGATIVE = Arrays.asList(
            "缺货登记", "售罄", "无票", "暂时缺货", "已售完", "不可购买"
    );
    private static final List<String> BUY_ENTRY = Arrays.asList(
            "立即购买", "选座购买", "立即预订", "马上抢", "立即抢购"
    );

    private static final Pattern PRICE_PATTERN = Pattern.compile("(?:¥|￥)?\\s*(\\d{2,5})\\s*(?:元)?");

    private long lastActionAt = 0L;
    private long lastTriggerAt = 0L;
    private String lastTriggerKey = "";

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        if (event == null || event.getPackageName() == null) return;
        if (!DAMAI_PACKAGE.contentEquals(event.getPackageName())) return;

        AccessibilityNodeInfo root = getRootInActiveWindow();
        if (root == null) return;

        List<NodeText> nodes = collectNodes(root);
        String fullText = joinTexts(nodes);
        saveScan(fullText);

        String blocker = firstContaining(fullText, BLOCKERS);
        if (blocker != null) {
            setArmed(false);
            saveStatus("检测到“" + blocker + "”，已暂停自动点击，请人工接管。" );
            notifyUser("需要人工接管", "检测到“" + blocker + "”，Ticket Radar 已暂停。" );
            return;
        }

        SharedPreferences sp = getSharedPreferences(MainActivity.PREFS, MODE_PRIVATE);
        boolean armed = sp.getBoolean(MainActivity.KEY_ARMED, false);
        if (!armed) {
            saveStatus("正在读取大麦页面；自动点击未开启。" );
            return;
        }

        long now = SystemClock.elapsedRealtime();
        if (now - lastActionAt < ACTION_COOLDOWN_MS) return;

        List<Integer> targets = parseTargets(sp.getString(MainActivity.KEY_PRICES, ""));
        NodeText ticket = findTargetTicket(nodes, targets);
        if (ticket != null) {
            String triggerKey = ticket.text + "@" + targets.toString();
            if (!triggerKey.equals(lastTriggerKey) || now - lastTriggerAt > DUPLICATE_TRIGGER_MS) {
                AccessibilityNodeInfo clickable = findClickable(ticket.node);
                if (clickable != null && clickable.performAction(AccessibilityNodeInfo.ACTION_CLICK)) {
                    lastActionAt = now;
                    lastTriggerAt = now;
                    lastTriggerKey = triggerKey;
                    saveStatus("发现目标票档并已点击：" + ticket.text);
                    notifyUser("发现可购票档", "已点击“" + ticket.text + "”，请留意后续页面。" );
                    return;
                }
            }
        }

        if (sp.getBoolean(MainActivity.KEY_AUTO_BUY, true)) {
            NodeText buy = findBuyEntry(nodes);
            if (buy != null) {
                AccessibilityNodeInfo clickable = findClickable(buy.node);
                if (clickable != null && clickable.performAction(AccessibilityNodeInfo.ACTION_CLICK)) {
                    lastActionAt = now;
                    saveStatus("已点击正常购票入口：“" + buy.text + "”，等待票档页面。" );
                    return;
                }
            }
        }

        if (containsAny(fullText, NEGATIVE)) {
            saveStatus("当前页面未发现目标可购票档。" );
        } else {
            saveStatus("已扫描当前页面，暂未匹配到目标票档。" );
        }
    }

    @Override
    public void onInterrupt() {
        saveStatus("无障碍服务被系统中断。" );
    }

    @Override
    protected void onServiceConnected() {
        super.onServiceConnected();
        createChannel();
        saveStatus("无障碍服务已连接。请打开大麦目标演出页面。" );
    }

    private List<NodeText> collectNodes(AccessibilityNodeInfo root) {
        List<NodeText> out = new ArrayList<>();
        ArrayDeque<AccessibilityNodeInfo> queue = new ArrayDeque<>();
        queue.add(root);
        int seen = 0;
        while (!queue.isEmpty() && seen < 800) {
            AccessibilityNodeInfo node = queue.removeFirst();
            seen++;
            String text = nodeText(node);
            if (!TextUtils.isEmpty(text)) out.add(new NodeText(node, text.trim()));
            for (int i = 0; i < node.getChildCount(); i++) {
                AccessibilityNodeInfo child = node.getChild(i);
                if (child != null) queue.addLast(child);
            }
        }
        return out;
    }

    private String nodeText(AccessibilityNodeInfo node) {
        StringBuilder sb = new StringBuilder();
        if (node.getText() != null) sb.append(node.getText());
        if (node.getContentDescription() != null) {
            if (sb.length() > 0) sb.append(" | ");
            sb.append(node.getContentDescription());
        }
        return sb.toString();
    }

    private String joinTexts(List<NodeText> nodes) {
        StringBuilder sb = new StringBuilder();
        int chars = 0;
        Set<String> dedupe = new HashSet<>();
        for (NodeText nt : nodes) {
            if (!dedupe.add(nt.text)) continue;
            if (chars > 12000) break;
            sb.append(nt.text).append('\n');
            chars += nt.text.length() + 1;
        }
        return sb.toString();
    }

    private NodeText findTargetTicket(List<NodeText> nodes, List<Integer> targets) {
        List<NodeText> candidates = new ArrayList<>();
        for (NodeText nt : nodes) {
            String context = localContext(nt.node);
            if (containsAny(context, NEGATIVE)) continue;

            Matcher m = PRICE_PATTERN.matcher(nt.text.replace(",", ""));
            while (m.find()) {
                try {
                    int price = Integer.parseInt(m.group(1));
                    if (price < 100 || price > 10000) continue;
                    if (targets.isEmpty() || targets.contains(price)) {
                        candidates.add(new NodeText(nt.node, "¥" + price));
                    }
                } catch (NumberFormatException ignored) {}
            }
        }
        if (candidates.isEmpty()) return null;
        if (targets.isEmpty()) return candidates.get(0);

        Collections.sort(candidates, Comparator.comparingInt(a -> {
            int p = extractPrice(a.text);
            int idx = targets.indexOf(p);
            return idx < 0 ? Integer.MAX_VALUE : idx;
        }));
        return candidates.get(0);
    }

    private NodeText findBuyEntry(List<NodeText> nodes) {
        for (String keyword : BUY_ENTRY) {
            for (NodeText nt : nodes) {
                if (!nt.text.contains(keyword)) continue;
                String context = localContext(nt.node);
                if (containsAny(context, NEGATIVE)) continue;
                if (findClickable(nt.node) != null) return nt;
            }
        }
        return null;
    }

    private AccessibilityNodeInfo findClickable(AccessibilityNodeInfo node) {
        AccessibilityNodeInfo cur = node;
        for (int i = 0; i < 5 && cur != null; i++) {
            if (cur.isClickable() && cur.isEnabled()) return cur;
            cur = cur.getParent();
        }
        return null;
    }

    private String localContext(AccessibilityNodeInfo node) {
        StringBuilder sb = new StringBuilder();
        AccessibilityNodeInfo parent = node.getParent();
        AccessibilityNodeInfo base = parent != null ? parent : node;
        appendSubtreeText(base, sb, 0, 2);
        return sb.toString();
    }

    private void appendSubtreeText(AccessibilityNodeInfo node, StringBuilder sb, int depth, int maxDepth) {
        if (node == null || depth > maxDepth || sb.length() > 1200) return;
        String t = nodeText(node);
        if (!TextUtils.isEmpty(t)) sb.append(t).append(' ');
        for (int i = 0; i < node.getChildCount(); i++) {
            appendSubtreeText(node.getChild(i), sb, depth + 1, maxDepth);
        }
    }

    private List<Integer> parseTargets(String raw) {
        List<Integer> out = new ArrayList<>();
        if (raw == null) return out;
        String[] parts = raw.replace('，', ',').split(",");
        for (String part : parts) {
            String digits = part.replaceAll("[^0-9]", "");
            if (digits.isEmpty()) continue;
            try {
                int p = Integer.parseInt(digits);
                if (p >= 100 && p <= 10000 && !out.contains(p)) out.add(p);
            } catch (NumberFormatException ignored) {}
        }
        return out;
    }

    private int extractPrice(String s) {
        Matcher m = PRICE_PATTERN.matcher(s);
        if (m.find()) {
            try { return Integer.parseInt(m.group(1)); } catch (Exception ignored) {}
        }
        return -1;
    }

    private boolean containsAny(String text, List<String> keys) {
        if (text == null) return false;
        for (String key : keys) if (text.contains(key)) return true;
        return false;
    }

    private String firstContaining(String text, List<String> keys) {
        if (text == null) return null;
        for (String key : keys) if (text.contains(key)) return key;
        return null;
    }

    private void saveScan(String text) {
        getSharedPreferences(MainActivity.PREFS, MODE_PRIVATE).edit()
                .putString(MainActivity.KEY_LAST_SCAN, text)
                .apply();
    }

    private void saveStatus(String text) {
        getSharedPreferences(MainActivity.PREFS, MODE_PRIVATE).edit()
                .putString(MainActivity.KEY_LAST_STATUS, text)
                .apply();
    }

    private void setArmed(boolean armed) {
        getSharedPreferences(MainActivity.PREFS, MODE_PRIVATE).edit()
                .putBoolean(MainActivity.KEY_ARMED, armed)
                .apply();
    }

    private void createChannel() {
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm = getSystemService(NotificationManager.class);
            NotificationChannel c = new NotificationChannel(CHANNEL, "Ticket Radar 提醒", NotificationManager.IMPORTANCE_HIGH);
            c.setDescription("票档命中与人工接管提醒");
            nm.createNotificationChannel(c);
        }
    }

    private void notifyUser(String title, String body) {
        createChannel();
        Intent i = new Intent(this, MainActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent pi = PendingIntent.getActivity(this, 0, i,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        android.app.Notification.Builder b = Build.VERSION.SDK_INT >= 26
                ? new android.app.Notification.Builder(this, CHANNEL)
                : new android.app.Notification.Builder(this);
        b.setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentTitle(title)
                .setContentText(body)
                .setAutoCancel(true)
                .setPriority(android.app.Notification.PRIORITY_HIGH)
                .setContentIntent(pi);
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        nm.notify((int) (System.currentTimeMillis() & 0x7fffffff), b.build());
    }

    private static class NodeText {
        final AccessibilityNodeInfo node;
        final String text;
        NodeText(AccessibilityNodeInfo node, String text) {
            this.node = node;
            this.text = text;
        }
    }
}
