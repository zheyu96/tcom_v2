#ifndef CONFIG_H
#define CONFIG_H

#include <iostream>
#include <cmath>
#include <vector>
#include <queue>
#include <set>
#include <map>
#include <random>
#include <functional>
#include <cassert>
#include <ctime>
#include <algorithm>
#include <utility>
#include <fstream>
#include <sstream>
#include <omp.h>

// #define double long double

// Shared approximation precision for all experiment drivers.
inline constexpr double EXPERIMENT_EPSILON = 0.9;

extern bool DEBUG;
extern double EPS;
extern double INF;
#endif