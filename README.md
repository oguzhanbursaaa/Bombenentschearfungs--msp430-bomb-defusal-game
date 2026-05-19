# 💣 MSP430 Bomb Defusal Game

A 3-phase interactive bomb defusal game built for the **MSP430F5529** 
microcontroller as part of the ETE104 course project (2025–2026).

## 🎮 Gameplay Phases
1. **Phase 1 – Potentiometer:** Find the secret target value (±9 tolerance)
2. **Phase 2 – Color Code (Simon Says):** Enter a 3-color sequence shown via RGB LED
3. **Phase 3 – Cut the Wire:** Disconnect the correct jumper cable before time runs out

## 🔌 Hardware
- MSP430F5529 LaunchPad
- I2C LCD display (address: 0x27)
- RGB LED (P1.2, P1.3, P1.4)
- Passive buzzer (P1.5)
- 3 push buttons: Red (P2.4), Green (P2.2), Blue (P2.0)
- 4 jumper cables: P6.1–P6.4
- Potentiometer (ADC12, Channel A0)

## ⚙️ Build
Open in Code Composer Studio and build for MSP430F5529.

## 📁 Files
- `Bombenentschärfungs Projekt Code.c` — Main source code
- `Bombenentschärfungs Projektbericht.pdf` — Project report (German)

## 📚 Course
ETE104 — 2025/2026 | Student ID: 220504057
