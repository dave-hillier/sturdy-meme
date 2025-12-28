// Complex number operations for ocean FFT shaders
// Prevent multiple inclusion
#ifndef OCEAN_COMPLEX_COMMON_GLSL
#define OCEAN_COMPLEX_COMMON_GLSL

// Complex numbers are represented as vec2: (real, imaginary)

// Complex multiplication: (a + bi)(c + di) = (ac - bd) + (ad + bc)i
vec2 complexMul(vec2 a, vec2 b) {
    return vec2(
        a.x * b.x - a.y * b.y,
        a.x * b.y + a.y * b.x
    );
}

// Complex conjugate: conj(a + bi) = a - bi
vec2 complexConj(vec2 c) {
    return vec2(c.x, -c.y);
}

// Euler's formula: exp(i * theta) = cos(theta) + i*sin(theta)
vec2 complexExp(float theta) {
    return vec2(cos(theta), sin(theta));
}

// Complex addition
vec2 complexAdd(vec2 a, vec2 b) {
    return a + b;
}

// Complex subtraction
vec2 complexSub(vec2 a, vec2 b) {
    return a - b;
}

// Complex division: (a + bi) / (c + di) = ((ac + bd) + (bc - ad)i) / (c^2 + d^2)
vec2 complexDiv(vec2 a, vec2 b) {
    float denom = dot(b, b); // c^2 + d^2
    return vec2(
        (a.x * b.x + a.y * b.y) / denom,
        (a.y * b.x - a.x * b.y) / denom
    );
}

// Complex magnitude (absolute value)
float complexAbs(vec2 c) {
    return length(c);
}

// Complex magnitude squared (faster than complexAbs)
float complexAbs2(vec2 c) {
    return dot(c, c);
}

#endif // OCEAN_COMPLEX_COMMON_GLSL
