/*
 * model-core/06-utils.js
 * Part 6/8: Utilities
 * Contains: Noise, Random, Perlin classes
 */
            g["com.watabou.utils.Noise"] = pg;
            pg.__name__ = "com.watabou.utils.Noise";
            pg.fractal = function(a, b, c) {
                null == c && (c = .5);
                null == b && (b = 1);
                null == a && (a = 1);
                for (var d = new pg, f = 1, h = 0; h < a;) {
                    h++;
                    var k = new Se(C.seed = 48271 * C.seed % 2147483647 | 0);
                    k.gridSize = b;
                    k.amplitude = f;
                    d.components.push(k);
                    b *= 2;
                    f *= c
                }
                return d
            };
            pg.prototype = {
                get: function(a, b) {
                    for (var c = 0, d = 0, f =
                            this.components; d < f.length;) {
                        var h = f[d];
                        ++d;
                        c += h.get(a, b)
                    }
                    return c
                },
                __class__: pg
            };
            var C = function() {};
            g["com.watabou.utils.Random"] = C;
            C.__name__ = "com.watabou.utils.Random";
            C.reset = function(a) {
                null == a && (a = -1);
                C.seed = -1 != a ? a : (new Date).getTime() % 2147483647 | 0
            };
            C.save = function() {
                return C.saved = C.seed
            };
            C.restore = function(a) {
                null == a && (a = -1); - 1 != a ? C.seed = a : -1 != C.saved && (C.seed = C.saved, C.saved = -1)
            };
            C.float = function() {
                return (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647
            };
            var Se = function(a) {
                this.offsetX = this.offsetY =
                    0;
                this.gridSize = this.amplitude = 1;
                for (var b = [], c = 0; 256 > c;) {
                    var d = c++;
                    b.push(Se.permutation[(d + a) % 256])
                }
                this.p = b;
                this.p = this.p.concat(this.p);
                if (null == Se.smooth) {
                    b = [];
                    for (c = 0; 4096 > c;) d = c++, a = d / 4096, b.push(a * a * a * (a * (6 * a - 15) + 10));
                    Se.smooth = b
                }
            };
            g["com.watabou.utils.Perlin"] = Se;
            Se.__name__ = "com.watabou.utils.Perlin";
            Se.prototype = {
                get: function(a, b) {
                    a = a * this.gridSize + this.offsetX;
                    0 > a && (a += 256);
                    b = b * this.gridSize + this.offsetY;
                    0 > b && (b += 256);
                    var c = Math.floor(a),
                        d = c + 1,
                        f = a - c,
                        h = Se.smooth[4096 * f | 0];
                    a = Math.floor(b);
                    var k = a + 1,
                        n = b - a,
                        p = Se.smooth[4096 * n | 0];
                    b = this.p[this.p[d] + a];
                    var g = this.p[this.p[c] + k];
                    d = this.p[this.p[d] + k];
                    switch (this.p[this.p[c] + a] & 3) {
                        case 0:
                            c = f + n;
                            break;
                        case 1:
                            c = f - n;
                            break;
                        case 2:
                            c = -f + n;
                            break;
                        case 3:
                            c = -f - n;
                            break;
                        default:
                            c = 0
                    }
                    a = f - 1;
                    switch (b & 3) {
                        case 0:
                            b = a + n;
                            break;
                        case 1:
                            b = a - n;
                            break;
                        case 2:
                            b = -a + n;
                            break;
                        case 3:
                            b = -a - n;
                            break;
                        default:
                            b = 0
                    }
                    k = c + (b - c) * h;
                    b = n - 1;
                    switch (g & 3) {
                        case 0:
                            c = f + b;
                            break;
                        case 1:
                            c = f - b;
                            break;
                        case 2:
                            c = -f + b;
                            break;
                        case 3:
                            c = -f - b;
                            break;
                        default:
                            c = 0
                    }
                    a = f - 1;
                    b = n - 1;
                    switch (d & 3) {
                        case 0:
                            b = a + b;
                            break;
                        case 1:
                            b =
                                a - b;
                            break;
                        case 2:
                            b = -a + b;
                            break;
                        case 3:
                            b = -a - b;
                            break;
                        default:
                            b = 0
                    }
                    return this.amplitude * (k + (c + (b - c) * h - k) * p)
                },
                __class__: Se
            };
            var Ae = function() {};
