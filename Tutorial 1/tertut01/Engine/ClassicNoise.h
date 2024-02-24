#pragma once
class ClassicNoise
{

private:
	static double dot(int g[], double x, double y, double z);
	static double mix(double a, double b, double t);
	static int fastfloor(double x);
	static double fade(double t);

public:
		ClassicNoise::ClassicNoise();
		ClassicNoise::~ClassicNoise();

public:
	static double noise(double x, double y, double z);
	//static int perm[512];
	//static int grad3[12][3];
	//static int p[256];

};

