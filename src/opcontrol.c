/** @file opcontrol.c
 * @brief File for operator control code
 *
 * This file should contain the user operatorControl() function and any functions related to it.
 *
 * Copyright (c) 2011-2014, Purdue University ACM SIG BOTS.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Purdue University ACM SIG BOTS nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PURDUE UNIVERSITY ACM SIG BOTS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Purdue Robotics OS contains FreeRTOS (http://www.freertos.org) whose source code may be
 * obtained from http://sourceforge.net/projects/freertos/files/ or on request.
 */

#include "main.h"

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "togglebtn.h"
#include "lfilter.h"

#define JOYSTICK_ARM

#define JOYSTICK_SLOT 1

#define CLAW_SPEED (-60)
#define CLAW_OPEN_DURATION 30
#define GRIP_STRENGTH (-40)

#define DRIVE_AXIS 3
#define TURN_AXIS 1

#ifdef JOYSTICK_ARM
#define ARM_AXIS 2
#else
#define ARM_BUTTON_GROUP 6
#endif
#define ARM_MAX_SPEED MAX_SPEED

#define LOW_SPEED_DIVISOR 2

void drive(int8_t drive, int8_t turn, bool squareInputs) {
	int16_t speed[2];
	int16_t absRawSpeed, maxRawSpeed;
	int8_t i;

	if (squareInputs) {
		float m;

		m = (float) drive / MAX_SPEED;
		m *= fabs(m);
		m *= MAX_SPEED;
		drive = m;

		m = (float) turn / MAX_SPEED;
		m *= fabs(m);
		m *= MAX_SPEED;
		turn = m;
	}

	speed[0] = drive + turn;		// left
	speed[1] = -drive + turn;		// right

	maxRawSpeed = 0;
	for (i = 0; i < 2; ++i) {
		absRawSpeed = abs(speed[i]);
		if (absRawSpeed > maxRawSpeed) {
			maxRawSpeed = absRawSpeed;
		}
	}

	if (maxRawSpeed > MAX_SPEED) {
		float scale = (float) maxRawSpeed / MAX_SPEED;
		speed[0] /= scale;
		speed[1] /= scale;
	}

	// Linear filtering for gradual acceleration and reduced motor wear
	speed[0] = getfSpeed(FRONT_LEFT_MOTOR_CHANNEL, speed[0]);
	speed[1] = getfSpeed(FRONT_RIGHT_MOTOR_CHANNEL, speed[1]);

	motorSet(FRONT_LEFT_MOTOR_CHANNEL, speed[0]);
	motorSet(FRONT_RIGHT_MOTOR_CHANNEL, speed[1]);
	motorSet(BACK_LEFT_MOTOR_CHANNEL, speed[0]);
	motorSet(BACK_RIGHT_MOTOR_CHANNEL, speed[1]);
}

/*
 * Runs the user operator control code. This function will be started in its own task with the
 * default priority and stack size whenever the robot is enabled via the Field Management System
 * or the VEX Competition Switch in the operator control mode. If the robot is disabled or
 * communications is lost, the operator control task will be stopped by the kernel. Re-enabling
 * the robot will restart the task, not resume it from where it left off.
 *
 * If no VEX Competition Switch or Field Management system is plugged in, the VEX Cortex will
 * run the operator control task. Be warned that this will also occur if the VEX Cortex is
 * tethered directly to a computer via the USB A to A cable without any VEX Joystick attached.
 *
 * Code running in this task can take almost any action, as the VEX Joystick is available and
 * the scheduler is operational. However, proper use of delay() or taskDelayUntil() is highly
 * recommended to give other tasks (including system tasks such as updating LCDs) time to run.
 *
 * This task should never exit; it should end with some kind of infinite loop, even if empty.
 */
void operatorControl()
{
    // misc controls
    bool lowSpeed = false;
    toggleBtnInit(JOYSTICK_SLOT, 8, JOY_LEFT);      // reset claw^M
    toggleBtnInit(JOYSTICK_SLOT, 8, JOY_RIGHT);     // toggle low speed mode

    // drive code
	int8_t driveSpeed, turnSpeed;

	// claw code
	bool closeClaw = false;
	int8_t i = 0;

	toggleBtnInit(JOYSTICK_SLOT, 8, JOY_DOWN);

	// arm code
	float armSpeed = 0;

	while (true) {
		// low speed mode toggle
		if (toggleBtnGet(JOYSTICK_SLOT, 8, JOY_RIGHT) == BUTTON_PRESSED) {
			lowSpeed = !lowSpeed;
		}

		// drive code
		driveSpeed = (int8_t) joystickGetAnalog(JOYSTICK_SLOT, DRIVE_AXIS);
#ifdef JOYSTICK_ARM
		if (joystickGetDigital(JOYSTICK_SLOT, 6, JOY_UP) || joystickGetDigital(JOYSTICK_SLOT, 6, JOY_DOWN)) {	// only rotate if not controlling arm
			turnSpeed = 0;
		} else {
			turnSpeed = (int8_t) joystickGetAnalog(JOYSTICK_SLOT, TURN_AXIS);
		}
#else
		turnSpeed = (int8_t) joystickGetAnalog(JOYSTICK_SLOT, TURN_AXIS);
#endif

		if (lowSpeed) {
			drive(driveSpeed / LOW_SPEED_DIVISOR, turnSpeed / LOW_SPEED_DIVISOR, true);
		} else {
			drive(driveSpeed, turnSpeed, true);
		}

		// claw code
		// MUST start with claw FULLY open
		if (toggleBtnGet(JOYSTICK_SLOT, 8, JOY_LEFT) == BUTTON_HELD) {
			motorSet(CLAW_MOTOR_CHANNEL, -CLAW_SPEED);
		} else if (toggleBtnGet(JOYSTICK_SLOT, 8, JOY_LEFT) == BUTTON_RELEASED) {
			motorSet(CLAW_MOTOR_CHANNEL, 0);
			closeClaw = false;
			i = 0;
		} else {
			if (toggleBtnGet(JOYSTICK_SLOT, 8, JOY_DOWN) == BUTTON_PRESSED) {
				closeClaw = !closeClaw;
			}

			if (closeClaw) {
				if (i < CLAW_OPEN_DURATION) {
					++i;
					motorSet(CLAW_MOTOR_CHANNEL, CLAW_SPEED);
				} else {
					motorSet(CLAW_MOTOR_CHANNEL, GRIP_STRENGTH);
				}
			} else if (i > 0) {
				--i;
				motorSet(CLAW_MOTOR_CHANNEL, -CLAW_SPEED);
			} else {
				motorSet(CLAW_MOTOR_CHANNEL, 0);
			}
		}

		// arm code
#ifdef JOYSTICK_ARM
		if (joystickGetDigital(JOYSTICK_SLOT, 6, JOY_UP)) {
			armSpeed = (float) joystickGetAnalog(JOYSTICK_SLOT, ARM_AXIS) / MAX_SPEED;
			armSpeed *= fabs(armSpeed) * ARM_MAX_SPEED;
		} else {
			armSpeed = 0;
		}
#else
		if (joystickGetDigital(JOYSTICK_SLOT, ARM_BUTTON_GROUP, JOY_UP)) {
			armSpeed = ARM_MAX_SPEED;
		} else if (joystickGetDigital(JOYSTICK_SLOT, ARM_BUTTON_GROUP, JOY_DOWN)) {
			armSpeed = -ARM_MAX_SPEED;
		} else {
			armSpeed = 0;
		}
#endif

		if (lowSpeed) {
			motorSet(LEFT_ARM_MOTOR_CHANNEL, (int) armSpeed / LOW_SPEED_DIVISOR);
			motorSet(RIGHT_ARM_MOTOR_CHANNEL, (int) -armSpeed / LOW_SPEED_DIVISOR);
		} else {
			motorSet(LEFT_ARM_MOTOR_CHANNEL, (int) armSpeed);
			motorSet(RIGHT_ARM_MOTOR_CHANNEL, (int) -armSpeed);
		}

		toggleBtnUpdateAll();
		delay(20);
	}
}

/** task example code
 * http://www.vexforum.com/index.php/10328-cody-s-pros-tutorial-series/p3#p96810
 * http://www.vexforum.com/index.php/10048-purdue-robotics-os-pros-now-available/p6#p95114
 */

/**	pid
 * http://www.vexforum.com/index.php/11759-pid-help
 */
