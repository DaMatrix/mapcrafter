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

#ifndef PROGRESS_H_
#define PROGRESS_H_

#include <cstddef> //size_t
#include <ctime> //std::time_t
#include <mutex>
#include <vector>

namespace mapcrafter {
namespace util {

/**
 * A basic interface for a progress handler.
 */
class IProgressHandler {
public:
	using progress_t = size_t;

	virtual ~IProgressHandler();

	virtual void begin(progress_t max) = 0;
	virtual void incrementValue(progress_t increment = 1) = 0;
};

class MultiplexingProgressHandler : public IProgressHandler {
	std::vector<IProgressHandler*> handlers;

public:
	MultiplexingProgressHandler();

	void addHandler(IProgressHandler* handler);

	void begin(progress_t max) override;
	void incrementValue(progress_t increment) override;
};

class AbstractOutputProgressHandler : public IProgressHandler {
public:
	AbstractOutputProgressHandler();

	void begin(progress_t max) override;
	void incrementValue(progress_t increment) override;
	virtual void finish();

protected:
	virtual void update(progress_t max, progress_t value, double percentage, double average_speed, int eta) = 0;

private:
	void dispatchUpdate(bool force);

	std::mutex update_mutex;

	progress_t max;
	progress_t value;

	// the time of the start of progress
	std::time_t start;
	// time of last update
	std::time_t last_update;
};

class LogOutputProgressHandler : public AbstractOutputProgressHandler {
public:
	LogOutputProgressHandler();

protected:
	void update(progress_t max, progress_t value, double percentage, double average_speed, int eta) override;

private:
	unsigned last_step;
};

/**
 * Shows a nice command line progress bar.
 */
class ProgressBar : public AbstractOutputProgressHandler {
public:
	ProgressBar();

	void finish() override;

protected:
	void update(progress_t max, progress_t value, double percentage, double average_speed, int eta) override;

private:
	// length of last output needed to clear the line
	unsigned last_output_len;
};

} /* namespace util */
} /* namespace mapcrafter */
#endif /* PROGRESS_H_ */
