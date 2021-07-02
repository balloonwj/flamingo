#pragma once

#include <stdint.h>
#include <algorithm>
#include <string>

using namespace std;
	
	class Timestamp
	{
	public:
		///
		/// Constucts an invalid Timestamp.
		///
		Timestamp() : microSecondsSinceEpoch_(0)
		{
		}

		///
		/// Constucts a Timestamp at specific time
		///
		/// @param microSecondsSinceEpoch
		explicit Timestamp(int64_t microSecondsSinceEpoch);

        Timestamp& operator+=(Timestamp lhs)
        {
            this->microSecondsSinceEpoch_ += lhs.microSecondsSinceEpoch_;
            return *this;
        }

        Timestamp& operator+=(int64_t lhs)
        {
            this->microSecondsSinceEpoch_ += lhs;
            return *this;
        }

        Timestamp& operator-=(Timestamp lhs)
        {
            this->microSecondsSinceEpoch_ -= lhs.microSecondsSinceEpoch_;
            return *this;
        }

        Timestamp& operator-=(int64_t lhs)
        {
            this->microSecondsSinceEpoch_ -= lhs;
            return *this;
        }

		void swap(Timestamp& that)
		{
			std::swap(microSecondsSinceEpoch_, that.microSecondsSinceEpoch_);
		}

		// default copy/assignment/dtor are Okay

		string toString() const;
		string toFormattedString(bool showMicroseconds = true) const;

		bool valid() const { return microSecondsSinceEpoch_ > 0; }

		// for internal usage.
		int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_; }
		time_t secondsSinceEpoch() const
		{
			return static_cast<time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond);
		}

		///
		/// Get time of now.
		///
		static Timestamp now();
		static Timestamp invalid();

		static const int kMicroSecondsPerSecond = 1000 * 1000;

	private:
		int64_t     microSecondsSinceEpoch_;
	};

	inline bool operator<(Timestamp lhs, Timestamp rhs)
	{
		return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
	}

	inline bool operator>(Timestamp lhs, Timestamp rhs)
	{
		return rhs < lhs;
	}

	inline bool operator<=(Timestamp lhs, Timestamp rhs)
	{
		return !(lhs > rhs);
	}

	inline bool operator>=(Timestamp lhs, Timestamp rhs)
	{
		return !(lhs < rhs);
	}

	inline bool operator==(Timestamp lhs, Timestamp rhs)
	{
		return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
	}

	inline bool operator!=(Timestamp lhs, Timestamp rhs)
	{
		return !(lhs == rhs);
	}

	///
	/// Gets time difference of two timestamps, result in seconds.
	///
	/// @param high, low
	/// @return (high-low) in seconds
	/// @c double has 52-bit precision, enough for one-microsecond
	/// resolution for next 100 years.
	inline double timeDifference(Timestamp high, Timestamp low)
	{
		int64_t diff = high.microSecondsSinceEpoch() - low.microSecondsSinceEpoch();
		return static_cast<double>(diff) / Timestamp::kMicroSecondsPerSecond;
	}

	///
	/// Add @c seconds to given timestamp.
	///
	/// @return timestamp+seconds as Timestamp
	///
	inline Timestamp addTime(Timestamp timestamp, int64_t microseconds)
	{
		//int64_t delta = static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
		return Timestamp(timestamp.microSecondsSinceEpoch() + microseconds);
	}

