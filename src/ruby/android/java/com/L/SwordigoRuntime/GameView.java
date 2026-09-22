package com.L.SwordigoRuntime;

import android.content.Context;
import android.graphics.PointF;
import android.opengl.GLSurfaceView;
import android.os.SystemClock;
import android.util.SparseArray;
import android.view.MotionEvent;
import com.touchfoo.swordigo.Native;

public class GameView extends GLSurfaceView {

    public static GameView instance;

    public static final int TOUCH_BEGAN     = 1;
    public static final int TOUCH_ENDED     = 2;
    public static final int TOUCH_CANCELLED = 3;
    public static final int TOUCH_MOVED     = 4;

    public class Touch {
        public int id;
        public PointF position;
        public PointF previousPosition;
        public PointF startPosition;
        public double startTime;
        public int tapCount;
        public double time;
    }

    private final SparseArray<Touch> activeTouches = new SparseArray<>();

    public GameView(Context context) {
        super(context);
        instance = this;
    }

    private static double now() {
        return SystemClock.uptimeMillis() / 1000.0;
    }

    private int getTouchIdForAction(MotionEvent event, int actionIndex) {
        return event.getPointerId(actionIndex) + 1;
    }

    private void updateTouch(Touch touch, MotionEvent event, int pointerIndex) {
        touch.previousPosition = touch.position;
        touch.position = new PointF(
                event.getX(pointerIndex),
                getHeight() - event.getY(pointerIndex)
        );
        touch.time = now();

        if (touch.tapCount > 0) {
            float dx = touch.position.x - touch.startPosition.x;
            float dy = touch.position.y - touch.startPosition.y;
            if (Math.sqrt(dx * dx + dy * dy) > 70.0f) {
                touch.tapCount = 0;
            } else if (touch.time - touch.startTime > 2.0d) {
                touch.tapCount = 0;
            }
        }
    }

    private void sendTouch(int phase, Touch touch) {
        queueEvent(() -> {
            try {
                Native.handleTouchEvent(
                        phase,
                        touch.id,
                        touch.time,
                        touch.position.x,
                        touch.position.y,
                        touch.previousPosition.x,
                        touch.previousPosition.y,
                        touch.tapCount
                );
            } catch (Throwable t) {
            }
        });
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int actionMasked = event.getActionMasked();
        int actionIndex = event.getActionIndex();

        switch (actionMasked) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int id = getTouchIdForAction(event, actionIndex);
                Touch touch = new Touch();
                touch.id = id;
                touch.position = new PointF(event.getX(actionIndex), getHeight() - event.getY(actionIndex));
                touch.startPosition = touch.position;
                touch.previousPosition = touch.position;
                touch.time = now();
                touch.startTime = touch.time;
                touch.tapCount = 1;
                activeTouches.put(id, touch);
                sendTouch(TOUCH_BEGAN, touch);
                break;
            }
            case MotionEvent.ACTION_MOVE: {
                int movedCount = 0;
                for (int i = 0; i < event.getPointerCount(); i++) {
                    int id = getTouchIdForAction(event, i);
                    Touch touch = activeTouches.get(id);
                    if (touch != null) {
                        updateTouch(touch, event, i);
                        sendTouch(TOUCH_MOVED, touch);
                        movedCount++;
                    }
                }
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int id = getTouchIdForAction(event, actionIndex);
                Touch touch = activeTouches.get(id);
                if (touch != null) {
                    updateTouch(touch, event, actionIndex);
                    sendTouch(TOUCH_ENDED, touch);
                    activeTouches.remove(id);
                }
                break;
            }
            case MotionEvent.ACTION_CANCEL: {
                for (int i = 0; i < activeTouches.size(); i++) {
                    Touch touch = activeTouches.valueAt(i);
                    sendTouch(TOUCH_CANCELLED, touch);
                }
                activeTouches.clear();
                break;
            }
        }
        return true;
    }
}
