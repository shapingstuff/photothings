const int MAX_MONTHS = 36;
const float BIRTH_CM = 50.0f;
const float MAX_CM = 95.0f;
const float GROWTH_ALPHA = 0.60f; // <1 => fast early growth
const float REVOLUTIONS_PER_MONTH = 1.0f; // how many wheel turns per month step
const int TICKS_PER_REV = 4096;
const int TICKS_PER_MONTH = int(TICKS_PER_REV * REVOLUTIONS_PER_MONTH);