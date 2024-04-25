#pragma once
class SimplexNoise
{

private:
	static double dot(int g[], double x, double y, double z);
	static double noise(double x, double y, double z);
	static int fastfloor(double x);

public:
	SimplexNoise::SimplexNoise();
	SimplexNoise::~SimplexNoise();


public:
	static double nNoise(double xin, double yin, double zin);
};

