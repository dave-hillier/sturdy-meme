/*
 * model-core/01-blueprint-building.js
 * Part 1/8: Blueprint and Building
 * Contains: Blueprint, Building classes
 */
/*
 * mfcg-model/05-model-core.js
 * Part 5/8: Core Model Classes
 * Contains: Blueprint, Building, Canal, Cell, City, District, Grower, etc.
 */
            g["com.watabou.mfcg.model.Blueprint"] = Fd;
            Fd.__name__ = "com.watabou.mfcg.model.Blueprint";
            Fd.create = function(a, b) {
                b = new Fd(a, b);
                b.name = null;
                b.pop = 0;
                b.greens = ba.get("green", !1);
                b.random = ba.get("random", !0);
                if (b.random) {
                    var c = (a + 30) / 80;
                    null == c && (c = .5);
                    b.walls = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 <
                        c;
                    c = a / 80;
                    null == c && (c = .5);
                    b.shanty = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c;
                    c = .5 + a / 100;
                    null == c && (c = .5);
                    b.citadel = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c;
                    c = b.walls ? a / (a + 30) : .5;
                    null == c && (c = .5);
                    b.inner = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c;
                    c = .9;
                    null == c && (c = .5);
                    b.plaza = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c;
                    c = a / 18;
                    null == c && (c = .5);
                    b.temple = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c;
                    c = .6666666666666666;
                    null == c && (c = .5);
                    b.river = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 <
                        c;
                    b.coast = .5 > (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647
                } else b.citadel = ba.get("citadel", !0), b.inner = ba.get("urban_castle", !1), b.plaza = ba.get("plaza", !0), b.temple = ba.get("temple", !0), b.walls = ba.get("walls", !0), b.shanty = ba.get("shantytown", !1), b.coast = ba.get("coast", !0), b.river = ba.get("river", !0);
                b.hub = ba.get("hub", !1);
                b.gates = ba.get("gates", -1);
                return b
            };
            Fd.similar = function(a) {
                var b = Fd.create(a.size, a.seed);
                b.name = a.name;
                return b
            };
            Fd.fromURL = function() {
                var a = za.getInt("size", 0),
                    b = za.getInt("seed",
                        C.seed);
                if (0 == a || 0 == b) return null;
                a = new Fd(a, b);
                a.name = za.get("name");
                a.pop = za.getInt("population", 0);
                a.citadel = za.getFlag("citadel", !0);
                a.inner = za.getFlag("urban_castle", !1);
                a.plaza = za.getFlag("plaza", !0);
                a.temple = za.getFlag("temple", !0);
                a.walls = za.getFlag("walls", !0);
                a.shanty = za.getFlag("shantytown", !1);
                a.river = za.getFlag("river", !1);
                a.coast = za.getFlag("coast", !0);
                a.greens = za.getFlag("greens", !1);
                a.hub = za.getFlag("hub", !1);
                a.gates = za.getInt("gates", -1);
                a.coastDir = parseFloat(za.get("sea", "0.0"));
                a.export = za.get("export");
                a.style = za.get("style");
                return a
            };
            Fd.prototype = {
                updateURL: function() {
                    za.reset();
                    za.set("size", this.size);
                    za.set("seed", this.seed);
                    null != this.name && za.set("name", this.name);
                    0 < this.pop && za.set("population", this.pop);
                    za.setFlag("citadel", this.citadel);
                    za.setFlag("urban_castle", this.inner);
                    za.setFlag("plaza", this.plaza);
                    za.setFlag("temple", this.temple);
                    za.setFlag("walls", this.walls);
                    za.setFlag("shantytown", this.shanty);
                    za.setFlag("coast", this.coast);
                    za.setFlag("river", this.river);
                    za.setFlag("greens", this.greens);
                    this.hub ? za.setFlag("hub") : za.set("gates", this.gates);
                    this.coast && za.set("sea", this.coastDir)
                },
                __class__: Fd
            };
            var Jd = function() {};
            g["com.watabou.mfcg.model.Building"] = Jd;
            Jd.__name__ = "com.watabou.mfcg.model.Building";
            Jd.create = function(a, b, c, d, f) {
                null == f && (f = 0);
                null == d && (d = !1);
                null == c && (c = !1);
                var h = Math.sqrt(b);
                b = I.distance(a[0], a[1]);
                var k = I.distance(a[1], a[2]),
                    n = I.distance(a[2], a[3]),
                    p = I.distance(a[3], a[0]);
                b = Math.ceil(Math.min(b, n) / h);
                h = Math.ceil(Math.min(k, p) / h);
                if (1 >= b || 1 >= h) return null;
                c = d ? Jd.getPlanSym(b, h) : c ? Jd.getPlanFront(b, h) : Jd.getPlan(b, h);
                for (d = k = 0; d < c.length;) p = c[d], ++d, p && ++k;
                if (k >= b * h) return null;
                a = Zk.grid(a, b, h, f);
                d = [];
                f = 0;
                for (b = a.length; f < b;) h = f++, c[h] && d.push(a[h]);
                return Jd.circumference(d)
            };
            Jd.getPlan = function(a, b, c) {
                null == c && (c = .5);
                for (var d = a * b, f = [], h = 0, k = d; h < k;) h++, f.push(!1);
                var n = Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * a),
                    p = Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * b);
                f[n + p * a] = !0;
                --d;
                k = h = n;
                for (var g =
                        p, q = p;;) {
                    n = Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * a);
                    p = Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * b);
                    var m = n + p * a;
                    !f[m] && (0 < n && f[m - 1] || 0 < p && f[m - a] || n < a - 1 && f[m + 1] || p < b - 1 && f[m + a]) && (h > n && (h = n), k < n && (k = n), g > p && (g = p), q < p && (q = p), f[m] = !0, --d);
                    0 < h || k < a - 1 || 0 < g || q < b - 1 ? n = !0 : 0 < d ? (n = c, null == n && (n = .5), n = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < n) : n = !1;
                    if (!n) break
                }
                return f
            };
            Jd.getPlanFront = function(a, b) {
                for (var c = a * b, d = [], f = 0, h = c; f < h;) {
                    var k = f++;
                    d.push(k < a)
                }
                c -= a;
                for (f = 0;;) {
                    h =
                        Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * a);
                    k = Math.floor(1 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * (b - 1));
                    var n = h + k * a;
                    !d[n] && (0 < h && d[n - 1] || 0 < k && d[n - a] || h < a - 1 && d[n + 1] || k < b - 1 && d[n + a]) && (f < k && (f = k), d[n] = !0, --c);
                    f >= b - 1 ? 0 < c ? (h = .5, null == h && (h = .5), h = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < h) : h = !1 : h = !0;
                    if (!h) break
                }
                return d
            };
            Jd.getPlanSym = function(a, b) {
                for (var c = Jd.getPlan(a, b, 0), d = 0; d < b;)
                    for (var f = d++, h = 0, k = a; h < k;) {
                        var n = h++,
                            p = f * a + n;
                        n = (f + 1) * a - 1 - n;
                        c[p] = c[n] = c[p] || c[n]
                    }
                return c
            };
            Jd.circumference = function(a) {
                if (0 == a.length) return [];
                if (1 == a.length) return a[0];
                for (var b = [], c = [], d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    for (var h = 0, k = f.length; h < k;) {
                        var n = h++,
                            p = f[n],
                            g = f[(n + 1) % f.length],
                            q = !0;
                        n = 0;
                        do
                            if (n = b.indexOf(g, n), -1 == n) break;
                            else if (c[n] == p) {
                            b.splice(n, 1);
                            c.splice(n, 1);
                            q = !1;
                            break
                        } else n += 1;
                        while (n < b.length);
                        q && (b.push(p), c.push(g))
                    }
                }
                d = f = 0;
                for (h = b.length; d < h;)
                    if (n = d++, b.lastIndexOf(b[n]) != n) {
                        f = n;
                        break
                    } d = b[f];
                h = c[f];
                a = [d];
                do a.push(h), h = c[b.indexOf(h)]; while (h != d);
                b = [];
                d = 0;
                for (h = a.length; d <
                    h;) n = d++, c = a[(n + 1) % a.length], f = a[(n + 2) % a.length], n = c.subtract(a[n]), f = f.subtract(c), .999 < (n.x * f.x + n.y * f.y) / n.get_length() / f.get_length() && b.push(c);
                for (d = 0; d < b.length;) f = b[d], ++d, N.remove(a, f);
                return a
            };
            var yb = function(a, b) {
                var c = Ua.toPolyline(b);
                if (0 < a.waterEdge.length) {
                    var d = c[0];
                    wd.set(d, qa.lerp(d, c[1]));
                    var f = a.shore,
                        h = f.indexOf(d);
                    2 <= h && wd.set(f[h - 1], qa.lerp(f[h - 1], qa.lerp(f[h - 2], d)));
                    h < f.length - 2 && wd.set(f[h + 1], qa.lerp(f[h + 1], qa.lerp(f[h + 2], d)))
                }
                Sa.set(c, uc.smoothOpen(c, null, 1));
                Ua.assignData(b,
                    Tc.CANAL);
                if (null != a.wall) {
                    d = a.wall.shape;
                    f = 1;
                    for (var k = b.length; f < k;) {
                        h = f++;
                        var n = c[h],
                            p = d.indexOf(n);
                        if (-1 != p) {
                            var g = c[h - 1],
                                q = c[h + 1].subtract(g),
                                m = d.length,
                                u = d[(p + m - 1) % m];
                            p = d[(p + 1) % m];
                            u = qa.intersectLines(g.x, g.y, q.x, q.y, u.x, u.y, p.x - u.x, p.y - u.y).x;
                            g = new I(g.x + q.x * u, g.y + q.y * u);
                            wd.set(c[h], qa.lerp(n, g))
                        }
                    }
                }
                this.city = a;
                this.course = b
            };
