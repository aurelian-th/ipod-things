import { motion, useMotionValue, useReducedMotion } from "framer-motion";
import React from "react";
import { Button } from "./Button";

interface IHoldButtonProps {
  onComplete: () => void;
  duration?: number;
  describedBy?: string;
}

const MOVE_TOLERANCE_PX = 12;

const HoldButton: React.FC<IHoldButtonProps> = ({ onComplete, duration = 3, describedBy }) => {
  const progress = useMotionValue(0);
  const reducedMotion = useReducedMotion();
  const timerRef = React.useRef<number | null>(null);
  const rafRef = React.useRef<number | null>(null);
  const resetTimerRef = React.useRef<number | null>(null);
  const startedAtRef = React.useRef<number>(0);
  const inputRef = React.useRef<"pointer" | "keyboard" | null>(null);
  const completedRef = React.useRef(false);
  const [holding, setHolding] = React.useState(false);
  const labelId = React.useId();

  const clearTimers = React.useCallback(() => {
    if (timerRef.current !== null) {
      window.clearTimeout(timerRef.current);
      timerRef.current = null;
    }
    if (rafRef.current !== null) {
      window.cancelAnimationFrame(rafRef.current);
      rafRef.current = null;
    }
    if (resetTimerRef.current !== null) {
      window.clearTimeout(resetTimerRef.current);
      resetTimerRef.current = null;
    }
  }, []);

  const cancel = React.useCallback(() => {
    if (completedRef.current || !inputRef.current) return;
    clearTimers();
    inputRef.current = null;
    setHolding(false);
    const startVal = progress.get();
    const fadeMs = reducedMotion ? 100 : 300;
    const fadeStart = performance.now();
    const stepReset = (now: number) => {
      const t = Math.min(1, (now - fadeStart) / fadeMs);
      progress.set(startVal * (1 - t));
      if (t < 1 && !inputRef.current && !completedRef.current) {
        rafRef.current = window.requestAnimationFrame(stepReset);
      } else {
        rafRef.current = null;
      }
    };
    rafRef.current = window.requestAnimationFrame(stepReset);
  }, [clearTimers, progress, reducedMotion]);

  React.useEffect(() => {
    const onVisibilityChange = () => {
      if (document.hidden) cancel();
    };
    window.addEventListener("blur", cancel);
    document.addEventListener("visibilitychange", onVisibilityChange);
    return () => {
      clearTimers();
      window.removeEventListener("blur", cancel);
      document.removeEventListener("visibilitychange", onVisibilityChange);
    };
  }, [cancel, clearTimers]);

  const start = (input: "pointer" | "keyboard") => {
    if (inputRef.current || completedRef.current) return;
    clearTimers();
    inputRef.current = input;
    setHolding(true);
    const durationMs = Math.max(100, duration * 1000);
    startedAtRef.current = performance.now();

    const tick = (now: number) => {
      if (!inputRef.current || completedRef.current) return;
      const ratio = Math.min(1, (now - startedAtRef.current) / durationMs);
      progress.set(ratio);
      if (ratio < 1) {
        rafRef.current = window.requestAnimationFrame(tick);
      }
    };
    rafRef.current = window.requestAnimationFrame(tick);

    timerRef.current = window.setTimeout(() => {
      if (!inputRef.current || document.hidden) {
        cancel();
        return;
      }
      clearTimers();
      progress.set(1);
      completedRef.current = true;
      inputRef.current = null;
      onComplete();
    }, durationMs);
  };

  return (
    <Button.Root
      intent="danger"
      className="w-52"
      aria-labelledby={labelId}
      aria-describedby={describedBy}
      data-holding={holding || undefined}
      onClick={(event) => event.preventDefault()}
      onContextMenu={(event) => {
        event.preventDefault();
        cancel();
      }}
      onPointerDown={(event) => {
        if (event.button !== 0 || !event.isPrimary) return;
        try {
          event.currentTarget.setPointerCapture(event.pointerId);
        } catch {
          // WKWebView on Intel macOS can throw if pointer capture is unavailable.
        }
        start("pointer");
      }}
      onPointerMove={(event) => {
        if (inputRef.current !== "pointer") return;
        const bounds = event.currentTarget.getBoundingClientRect();
        if (
          event.clientX < bounds.left - MOVE_TOLERANCE_PX ||
          event.clientX > bounds.right + MOVE_TOLERANCE_PX ||
          event.clientY < bounds.top - MOVE_TOLERANCE_PX ||
          event.clientY > bounds.bottom + MOVE_TOLERANCE_PX
        ) {
          cancel();
        }
      }}
      onPointerUp={() => {
        if (inputRef.current === "pointer") cancel();
      }}
      onPointerCancel={cancel}
      onLostPointerCapture={() => {
        if (inputRef.current === "pointer") cancel();
      }}
      onBlur={() => {
        if (inputRef.current === "keyboard") cancel();
      }}
      onKeyDown={(event) => {
        if (event.key === "Escape") cancel();
        if (event.key !== " " && event.key !== "Enter") return;
        event.preventDefault();
        if (!event.repeat) start("keyboard");
      }}
      onKeyUp={(event) => {
        if (event.key !== " " && event.key !== "Enter") return;
        event.preventDefault();
        if (inputRef.current === "keyboard") cancel();
      }}
    >
      <motion.span
        className="pointer-events-none absolute inset-0 z-10 origin-left bg-[#601815]/35"
        style={{ scaleX: progress }}
        aria-hidden="true"
      />
      <Button.Label id={labelId}>{holding ? "Keep holding…" : "I understand"}</Button.Label>
    </Button.Root>
  );
};

export { HoldButton };
