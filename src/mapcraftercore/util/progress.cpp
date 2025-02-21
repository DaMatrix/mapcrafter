/*
 * Copyright 2012-2016 Moritz Hilscher
 *
 * This file is part of Mapcrafter.
 *
 * Mapcrafter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mapcrafter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Mapcrafter.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "progress.h"

#include "logging.h"
#include "other.h"
#include "../compat/nullptr.h"

#include <iomanip>
#include <iostream>
#include <string>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <ctime>
#if defined(HAVE_SYS_IOCTL_H) && defined(HAVE_UNISTD_H)
#  include <sys/ioctl.h> // ioctl, TIOCGWINSZ
#  include <unistd.h> // STDOUT_FILENO
#endif
#if defined(OS_WINDOWS)
#  include <windows.h>
#endif

namespace mapcrafter {
namespace util {

static std::string format_eta(int eta) {
	int MINUTES = 60;
	int HOURS = 60*MINUTES;
	int DAYS = 24*HOURS;

	int days = eta / DAYS;
	eta -= days * DAYS;
	int hours = eta / HOURS;
	eta -= hours * HOURS;
	int minutes = eta / MINUTES;
	eta -= minutes * MINUTES;
	int seconds = eta;

	std::string str_days = std::to_string(days) + "d";
	std::string str_hours = std::to_string(hours) + "h";
	std::string str_minutes = std::to_string(minutes) + "m";
	if (minutes < 10)
		str_minutes = "0" + str_minutes;
	std::string str_seconds = std::to_string(seconds) + "s";
	if (seconds < 10)
		str_seconds = "0" + str_seconds;

	if (days > 0)
		return str_days + " " + str_hours;
	if (hours > 0)
		return str_hours + " " + str_minutes;
	if (minutes > 0)
		return str_minutes + " " + str_seconds;
	return str_seconds;
}

IProgressHandler::~IProgressHandler() = default;

MultiplexingProgressHandler::MultiplexingProgressHandler() {
}

void MultiplexingProgressHandler::addHandler(IProgressHandler* handler) {
	handlers.push_back(handler);
}

void MultiplexingProgressHandler::begin(progress_t max) {
	for (auto& handler : handlers)
		handler->begin(max);
}

void MultiplexingProgressHandler::incrementValue(progress_t increment) {
	for (auto& handler : handlers)
		handler->incrementValue(increment);
}

AbstractOutputProgressHandler::AbstractOutputProgressHandler()
	: max(0), value(0), start(0), last_update(0) {
}

void AbstractOutputProgressHandler::begin(progress_t max) {
	std::lock_guard<std::mutex> update_lock(update_mutex);
	assert(start == 0 && "already started??");
	this->max = max;
	this->value = 0;
	start = std::time(nullptr);
	last_update = 0;
	dispatchUpdate(true);
}

void AbstractOutputProgressHandler::incrementValue(progress_t increment) {
	std::lock_guard<std::mutex> update_lock(update_mutex);
	assert(start != 0 && "not started yet?");
	assert(value + increment <= max);
	value += increment;
	dispatchUpdate(false);
}

void AbstractOutputProgressHandler::finish() {
	std::lock_guard<std::mutex> update_lock(update_mutex);
	if (start == 0)
		return; //do nothing if not yet started
	value = max;
	dispatchUpdate(true);
}

void AbstractOutputProgressHandler::dispatchUpdate(bool force) {
	std::time_t now = std::time(nullptr);

	// check whether the time since the last shown update
	// and the change was big enough to show a new update
	bool should_show_update = force || value == max || last_update < now;
	if (!should_show_update)
		return;

	double percentage = value / (double) max * 100.;

	// now calculate the average speed
	double average_speed = (double) value / (now - start);

	// eta only when we have an average speed
	int eta = -1;
	if (value != max && value != 0 && (now - start) != 0)
		eta = (max - value) / average_speed;

	// set this as last update
	last_update = now;

	// call handler
	update(max, value, percentage, average_speed, eta);
}

LogOutputProgressHandler::LogOutputProgressHandler()
	: last_step(0) {
}

void LogOutputProgressHandler::update(progress_t max, progress_t value, double percentage, double average_speed,
		int eta) {
	if (percentage < last_step + 5)
		return;
	last_step = percentage;

	// TODO maybe make it possible to specify a format?
	auto log = LOGN(INFO, "progress");
	log << std::floor(percentage) << "% complete. ";
	log << "Processed " << value << "/" << max << " items ";
	log << "with average " << std::setprecision(1) << std::fixed << average_speed << "/s.";
	if (eta >= 0)
		log << " ETA " << util::format_eta(eta) << ".";
}

ProgressBar::ProgressBar()
	: last_output_len(0) {
}

static std::string createProgressBar(unsigned width, double percentage) {
	// width - 2 because we need two characters for [ and ]
	width -= 2;

	std::string progressbar = "[";
	double progress_step = (double) 100 / width;
	for (int i = 0; i < width; i++) {
		double current = progress_step * i;
		if (current > percentage)
			progressbar += " ";
		else if (percentage - progress_step < current)
			progressbar += ">";
		else
			progressbar += "=";
	}
	return progressbar + "]";
}

static std::string createProgressStats(double percentage, IProgressHandler::progress_t value,
	   IProgressHandler::progress_t max, double speed_average, int eta) {
	std::string stats;
	char formatted_percent[20], formatted_speed_average[20];
	sprintf(&formatted_percent[0], "%.2f%%", percentage);
	sprintf(&formatted_speed_average[0], "%.2f", speed_average);
	stats.append(formatted_percent).append(" ");
	stats.append(std::to_string(value)).append("/").append(std::to_string(max)).append(" ");
	stats.append(formatted_speed_average).append("/s ");

	if (eta >= 0)
		stats.append("ETA ").append(format_eta(eta));

	// add some padding to these stats
	// to prevent the progress bar changing the size all the time
	int padding = 20 - (stats.size() % 20);
	stats.append(padding, ' ');
	return stats;
}

void ProgressBar::update(progress_t max, progress_t value, double percentage, double average_speed, int eta) {
	// try to determine the width of the terminal
	// use 80 columns as default if we can't determine a terminal size
	int terminal_width = 80;
#ifdef TIOCGWINSZ
	struct winsize ws = {0, 0, 0, 0};
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	if (ws.ws_col != 0)
		terminal_width = ws.ws_col;
#elif defined(OS_WINDOWS)
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
		terminal_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
		// seems Windows "terminal" has some problems if we use the full terminal width
		// so let's just pretend the terminal is a bit smaller
		terminal_width -= 2;
	}
#endif

	// create the progress stats: percentage, current/maximum value, speed, eta
	std::string stats;
	stats = createProgressStats(percentage, value, max, average_speed, eta);

	// now create the progress bar
	// with the remaining size minus one as size
	// (because the space between progress and stats)
	int progressbar_width = terminal_width - stats.size() - 1;
	std::string progressbar = createProgressBar(progressbar_width, percentage);

	// go to the begin of the line and clear it
	std::cout << "\r" << std::string(last_output_len, ' ') << "\r";

	// now show everything
	// also go back to beginning of line after it, in case there is other output
	std::cout << progressbar << " " << stats << "\r" << std::flush;

	// set this as last shown
	last_output_len = progressbar.size() + 1 + stats.size();
}

void ProgressBar::finish() {
	AbstractOutputProgressHandler::finish();
	std::cout << std::endl;
}

} /* namespace util */
} /* namespace mapcrafter */
