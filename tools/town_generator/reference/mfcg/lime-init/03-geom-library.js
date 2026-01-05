/*
 * lime-init/03-geom-library.js
 * Part 3/8: Geometry Library
 * Contains: com.watabou.geom.*, Delaunator, polygon operations
 */
            g["com.watabou.geom.Chaikin"] = Hf;
            Hf.__name__ = "com.watabou.geom.Chaikin";
            Hf.render = function(a, b, c, d) {
                null == c && (c = 1);
                for (var f = 0; f < c;) {
                    f++;
                    for (var h = [], k = a.length, n = 1, p = k - 1; n < p;) {
                        var P = n++,
                            g = a[P];
                        null == d || -1 == d.indexOf(g) ? (h.push(qa.lerp(g, a[P - 1], .25)), h.push(qa.lerp(g, a[P + 1], .25))) : h.push(g)
                    }
                    b ? (n = a[k - 1], null == d || -1 == d.indexOf(n) ? (h.push(qa.lerp(n, a[k - 2], .25)), h.push(qa.lerp(n, a[0], .25))) : h.push(n), n = a[0], null ==
                        d || -1 == d.indexOf(n) ? (h.push(qa.lerp(n, a[k - 1], .25)), h.push(qa.lerp(n, a[1], .25))) : h.push(n)) : (h.unshift(a[0]), h.push(a[k - 1]));
                    a = h
                }
                return a
            };
            var Ea = function(a, b) {
                null == b && (b = 0);
                this.c = a;
                this.r = b
            };
            g["com.watabou.geom.Circle"] = Ea;
            Ea.__name__ = "com.watabou.geom.Circle";
            Ea.prototype = {
                __class__: Ea
            };
            var Gc = function() {};
            g["com.watabou.geom.Color"] = Gc;
            Gc.__name__ = "com.watabou.geom.Color";
            Gc.rgbfSafe = function(a, b, c) {
                return (Fc.gate(255 * a, 0, 255) | 0) << 16 | (Fc.gate(255 * b, 0, 255) | 0) << 8 | Fc.gate(255 * c, 0, 255) | 0
            };
            Gc.lerp =
                function(a, b, c) {
                    null == c && (c = .5);
                    var d = a >>> 8 & 255,
                        f = a & 255,
                        h = b >>> 16,
                        k = b >>> 8 & 255;
                    b &= 255;
                    var n = 1 - c;
                    return (cb.toFloat(a >>> 16) * n + cb.toFloat(h) * c | 0) << 16 | (cb.toFloat(d) * n + cb.toFloat(k) * c | 0) << 8 | cb.toFloat(f) * n + cb.toFloat(b) * c | 0
                };
            Gc.scale = function(a, b) {
                return Gc.rgbfSafe(cb.toFloat(a >>> 16) / cb.toFloat(255) * b, cb.toFloat(a >>> 8 & 255) / cb.toFloat(255) * b, cb.toFloat(a & 255) / cb.toFloat(255) * b)
            };
            Gc.hsv = function(a, b, c) {
                var d = function(a) {
                        a -= 360 * Math.floor(a / 360);
                        return Fc.gate(Math.abs(a / 60 - 3) - 1, 0, 1)
                    },
                    f = d(a),
                    h = d(a - 120);
                a =
                    d(a + 120);
                return Gc.rgbfSafe((f * b + 1 - b) * c, (h * b + 1 - b) * c, (a * b + 1 - b) * c)
            };
            Gc.rgb2hsv = function(a) {
                var b = cb.toFloat(a >>> 16) / cb.toFloat(255),
                    c = cb.toFloat(a >>> 8 & 255) / cb.toFloat(255);
                a = cb.toFloat(a & 255) / cb.toFloat(255);
                var d = Math.min(b, Math.min(c, a)),
                    f = Math.max(b, Math.max(c, a));
                return d == f ? new ch(0, 0, d) : new ch(60 * ((b == d ? 3 : a == d ? 1 : 5) - (b == d ? c - a : a == d ? b - c : a - b) / (f - d)), (f - d) / f, f)
            };
            var Me = function() {};
            g["com.watabou.geom.Cubic"] = Me;
            Me.__name__ = "com.watabou.geom.Cubic";
            Me.smoothOpen = function(a, b) {
                null == b && (b = 4);
                var c =
                    function(a, b, c) {
                        a = b.subtract(a);
                        b = c.subtract(b);
                        var d = a.get_length() * b.get_length(),
                            f = (a.x * b.y - a.y * b.x) / d;
                        a = (a.x * b.x + a.y * b.y) / d;
                        return c.add(new I(b.x * a - b.y * f, b.y * a + b.x * f))
                    },
                    d = a.length;
                if (2 >= d) return a;
                var f = c(a[2], a[1], a[0]);
                c = c(a[d - 3], a[d - 2], a[d - 1]);
                a = a.slice();
                a.unshift(f);
                a.push(c);
                f = [];
                c = 1;
                for (var h = a.length - 2; c < h;) {
                    var k = c++,
                        n = a[k],
                        p = a[k + 1],
                        P = a[k + 2],
                        g = n,
                        m = p;
                    k = n.subtract(a[k - 1]);
                    k.normalize(1);
                    n = p.subtract(n);
                    n.normalize(1);
                    p = P.subtract(p);
                    p.normalize(1);
                    P = k.add(n);
                    P.normalize(1);
                    p = n.add(p);
                    p.normalize(1);
                    k = I.distance(g, m);
                    n = 1 / (1 + (P.x * n.x + P.y * n.y) + (p.x * n.x + p.y * n.y));
                    var q = k * n;
                    P = new I(g.x + P.x * q, g.y + P.y * q);
                    n *= -k;
                    n = new I(m.x + p.x * n, m.y + p.y * n);
                    f.push(g);
                    f.push(P);
                    f.push(n);
                    f.push(m)
                }
                b = Me.build(f, b);
                b.push(a[d].clone());
                return b
            };
            Me.smoothClosed = function(a, b) {
                null == b && (b = 4);
                for (var c = a.length, d = [], f = 0; f < c;) {
                    var h = f++,
                        k = a[h],
                        n = a[(h + 1) % c],
                        p = a[(h + 2) % c],
                        P = k,
                        g = n;
                    h = k.subtract(a[(h - 1 + c) % c]);
                    h.normalize(1);
                    k = n.subtract(k);
                    k.normalize(1);
                    n = p.subtract(n);
                    n.normalize(1);
                    p = h.add(k);
                    p.normalize(1);
                    n = k.add(n);
                    n.normalize(1);
                    h = I.distance(P, g);
                    k = 1 / (1 + (p.x * k.x + p.y * k.y) + (n.x * k.x + n.y * k.y));
                    var m = h * k;
                    p = new I(P.x + p.x * m, P.y + p.y * m);
                    k *= -h;
                    k = new I(g.x + n.x * k, g.y + n.y * k);
                    d.push(P);
                    d.push(p);
                    d.push(k);
                    d.push(g)
                }
                return Me.build(d, b)
            };
            Me.build = function(a, b) {
                null == b && (b = 4);
                var c = a.length,
                    d = [],
                    f = 0;
                do {
                    var h = a[f++],
                        k = a[f++],
                        n = a[f++],
                        p = a[f++];
                    d.push(h.clone());
                    for (var P = 1, g = b; P < g;) {
                        var m = P++;
                        d.push(Me.cubic(h, k, n, p, m / b))
                    }
                } while (f < c);
                return d
            };
            Me.cubic = function(a, b, c, d, f) {
                var h = 1 - f,
                    k = h * h * h;
                a = new I(a.x * k, a.y * k);
                k = 3 * h * h * f;
                a.x +=
                    b.x * k;
                a.y += b.y * k;
                k = 3 * h * f * f;
                a.x += c.x * k;
                a.y += c.y * k;
                k = f * f * f;
                a.x += d.x * k;
                a.y += d.y * k;
                return a
            };
            var Ic = function(a, b) {
                this.vertices = new pa;
                this.edges = [];
                this.faces = [];
                for (var c = 0; c < a.length;) {
                    var d = a[c];
                    ++c;
                    for (var f = [], h = 0; h < d.length;) {
                        var k = d[h];
                        ++h;
                        k = new kg(this.addVertex(k));
                        this.edges.push(k);
                        f.push(k)
                    }
                    h = new Wh(f[0]);
                    this.faces.push(h);
                    var n = d.length;
                    d = 0;
                    for (var p = n; d < p;) {
                        k = d++;
                        var P = f[k];
                        P.next = f[(k + 1) % n];
                        P.face = h
                    }
                }
                c = 0;
                for (h = this.edges; c < h.length;)
                    if (a = h[c], ++c, null == a.twin)
                        for (f = a.origin, k = a.next.origin.edges,
                            d = 0; d < k.length;)
                            if (n = k[d], ++d, n.next.origin == f) {
                                a.twin = n;
                                n.twin = a;
                                break
                            } if (null != b)
                    for (c = 0, h = this.faces.length; c < h;) k = c++, this.faces[k].data = b[k]
            };
            g["com.watabou.geom.DCEL"] = Ic;
            Ic.__name__ = "com.watabou.geom.DCEL";
            Ic.floodFill = function(a, b) {
                var c = [];
                a = [a];
                var d = new pa;
                for (null == b && (b = function(a) {
                        return !0
                    }); 0 < a.length;) {
                    var f = a.pop();
                    if (b(f)) {
                        c.push(f);
                        for (var h = f.halfEdge, k = h, n = !0; n;) {
                            var p = k;
                            k = k.next;
                            n = k != h;
                            null != p.twin && (p = p.twin.face, null == d.h.__keys__[p.__id__] && Z.add(a, p))
                        }
                    }
                    d.set(f, !0)
                }
                return c
            };
            Ic.floodFillEx = function(a, b) {
                for (var c = [a], d = [], f = a = a.halfEdge, h = !0; h;) {
                    var k = f;
                    f = f.next;
                    h = f != a;
                    null != k.twin && d.push(k)
                }
                for (; 0 < d.length;)
                    if (k = d.pop(), a = k.twin.face, -1 == c.indexOf(a) && b(k))
                        for (c.push(a), f = a = a.halfEdge, h = !0; h;) k = f, f = f.next, h = f != a, null != k.twin && d.push(k);
                return c
            };
            Ic.split = function(a) {
                for (var b = [], c = gf.fromArray(a); !gf.isEmpty(c);) {
                    for (var d = null, f = 0; f < a.length;) {
                        var h = a[f];
                        ++f;
                        if (null != c.h.__keys__[h.__id__]) {
                            d = h;
                            break
                        }
                    }
                    d = Ic.floodFill(d, function(a) {
                        return null != c.h.__keys__[a.__id__]
                    });
                    gf.removeArr(c, d);
                    b.push(d)
                }
                return b
            };
            Ic.circumference = function(a, b) {
                var c = gf.fromArray(b);
                if (null == a)
                    for (var d = Infinity, f = 0; f < b.length;) {
                        var h = b[f];
                        ++f;
                        for (var k = h = h.halfEdge, n = !0; n;) {
                            var p = k;
                            k = k.next;
                            n = k != h;
                            var P = p;
                            if (null == P.twin || null == c.h.__keys__[P.twin.face.__id__]) p = P.origin.point.x, d > p && (d = p, a = P)
                        }
                    }
                p = [];
                P = a;
                do
                    for (p.push(P), P = P.next; null != P.twin && null != c.h.__keys__[P.twin.face.__id__];) P = P.twin.next; while (P != a);
                return p
            };
            Ic.outline = function(a) {
                for (var b = gf.fromArray(a), c = [], d = 0; d < a.length;) {
                    var f =
                        a[d];
                    ++d;
                    for (var h = f = f.halfEdge, k = !0; k;) {
                        var n = h;
                        h = h.next;
                        k = h != f;
                        null != n.twin && null != b.h.__keys__[n.twin.face.__id__] || c.push(n)
                    }
                }
                b = null;
                for (d = []; !Z.isEmpty(c);) f = Ic.circumference(c[0], a), Z.removeAll(c, f), 0 < Sa.area(Ua.toPoly(f)) ? b = f : d.push(f);
                d.unshift(b);
                return d
            };
            Ic.prototype = {
                addVertex: function(a) {
                    var b = this.vertices.h[a.__id__];
                    if (null == b) {
                        b = this.vertices;
                        var c = new ak(a);
                        b.set(a, c);
                        return c
                    }
                    return b
                },
                addFace: function(a) {
                    for (var b = a.length, c = [], d = 0; d < a.length;) {
                        var f = a[d];
                        ++d;
                        f = new kg(f);
                        this.edges.push(f);
                        c.push(f)
                    }
                    a = new Wh(c[0]);
                    this.faces.push(a);
                    d = 0;
                    for (var h = b; d < h;) {
                        var k = d++;
                        f = c[k];
                        f.next = c[(k + 1) % b];
                        f.face = a
                    }
                    d = 0;
                    for (h = b; d < h;) {
                        k = d++;
                        f = c[k];
                        b = f.origin;
                        k = 0;
                        for (var n = f.next.origin.edges; k < n.length;) {
                            var p = n[k];
                            ++k;
                            if (p.next.origin == b) {
                                f.twin = p;
                                p.twin = f;
                                break
                            }
                        }
                    }
                    return a
                },
                removeFace: function(a) {
                    N.remove(this.faces, a);
                    var b = a.halfEdge;
                    do {
                        null != b.twin && (b.twin.twin = null);
                        var c = b.origin,
                            d = c.edges;
                        N.remove(d, b);
                        N.remove(this.edges, b);
                        0 == d.length && this.vertices.remove(c.point);
                        b = b.next
                    } while (b != a.halfEdge)
                },
                splitFace: function(a, b, c) {
                    for (var d = null, f = 0, h = b.edges; f < h.length;) {
                        var k = h[f];
                        ++f;
                        if (k.face == a) {
                            d = k;
                            break
                        }
                    }
                    for (f = [c]; d.origin != c;) f.push(d.origin), d = d.next;
                    for (h = [b]; d.origin != b;) h.push(d.origin), d = d.next;
                    this.removeFace(a);
                    this.addFace(f);
                    this.addFace(h);
                    f = 0;
                    for (h = b.edges; f < h.length;)
                        if (k = h[f], ++f, k.next.origin == c) return k;
                    return null
                },
                splitEdge: function(a, b) {
                    null == b && (b = qa.lerp(a.origin.point, a.next.origin.point));
                    var c = this.addVertex(b);
                    b = new kg(c);
                    b.face = a.face;
                    b.next = a.next;
                    a.next = b;
                    this.edges.push(b);
                    var d = a.twin;
                    null != d && (c = new kg(c), c.face = d.face, c.next = d.next, d.next = c, a.twin = c, b.twin = d, d.twin = b, c.twin = a, this.edges.push(c));
                    return b
                },
                collapseEdge: function(a) {
                    var b = a.origin,
                        c = a.next.origin;
                    b.point.setTo((b.point.x + c.point.x) / 2, (b.point.y + c.point.y) / 2);
                    a.face.halfEdge == a && (a.face.halfEdge = a.next);
                    a.prev().next = a.next;
                    N.remove(b.edges, a);
                    N.remove(this.edges, a);
                    a = a.twin;
                    null != a && (a.face.halfEdge == a && (a.face.halfEdge = a.next), a.prev().next = a.next, N.remove(c.edges, a), N.remove(this.edges, a));
                    a = 0;
                    for (var d =
                            c.edges; a < d.length;) {
                        var f = d[a];
                        ++a;
                        b.edges.push(f);
                        f.origin = b
                    }
                    this.vertices.remove(c.point);
                    return b
                },
                getEdge: function(a, b) {
                    var c = 0;
                    for (a = a.edges; c < a.length;) {
                        var d = a[c];
                        ++c;
                        if (d.next.origin == b) return d
                    }
                    return null
                },
                vertices2chain: function(a) {
                    for (var b = [], c = 1, d = a.length; c < d;) {
                        var f = c++;
                        b.push(this.getEdge(a[f - 1], a[f]))
                    }
                    return b
                },
                __class__: Ic
            };
            var kg = function(a) {
                this.origin = a;
                a.edges.push(this)
            };
            g["com.watabou.geom.HalfEdge"] = kg;
            kg.__name__ = "com.watabou.geom.HalfEdge";
            kg.prototype = {
                prev: function() {
                    for (var a =
                            this; a.next != this;) a = a.next;
                    return a
                },
                __class__: kg
            };
            var ak = function(a) {
                this.edges = [];
                this.point = a
            };
            g["com.watabou.geom.Vertex"] = ak;
            ak.__name__ = "com.watabou.geom.Vertex";
            ak.prototype = {
                __class__: ak
            };
            var Wh = function(a) {
                this.halfEdge = a
            };
            g["com.watabou.geom.Face"] = Wh;
            Wh.__name__ = "com.watabou.geom.Face";
            Wh.prototype = {
                getNeighbours: function() {
                    for (var a = [], b = this.halfEdge, c = b, d = !0; d;) {
                        var f = c;
                        c = c.next;
                        d = c != b;
                        null != f.twin && a.push(f.twin.face)
                    }
                    return a
                },
                getPoly: function() {
                    for (var a = [], b = this.halfEdge, c = b, d = !0; d;) {
                        var f = c;
                        c = c.next;
                        d = c != b;
                        a.push(f.origin.point)
                    }
                    return a
                },
                __class__: Wh
            };
            var Qb = function(a) {
                this.points = a;
                a = a.length;
                var b = 2 * a - 5,
                    c = 3 * b,
                    d = null,
                    f = null,
                    h = null,
                    k = null,
                    n = null;
                this.triangles = c = null != c ? new Uint32Array(c) : null != d ? new Uint32Array(d) : null != f ? new Uint32Array(f.__array) : null != h ? new Uint32Array(h) : null != k ? null == n ? new Uint32Array(k, 0) : new Uint32Array(k, 0, n) : null;
                c = 3 * b;
                n = k = h = f = d = null;
                this.halfedges = c = null != c ? new Int32Array(c) : null != d ? new Int32Array(d) : null != f ? new Int32Array(f.__array) : null !=
                    h ? new Int32Array(h) : null != k ? null == n ? new Int32Array(k, 0) : new Int32Array(k, 0, n) : null;
                this.hashSize = Math.ceil(Math.sqrt(a));
                n = k = h = f = d = null;
                this.hullPrev = c = null != a ? new Uint32Array(a) : null != d ? new Uint32Array(d) : null != f ? new Uint32Array(f.__array) : null != h ? new Uint32Array(h) : null != k ? null == n ? new Uint32Array(k, 0) : new Uint32Array(k, 0, n) : null;
                n = k = h = f = d = null;
                this.hullNext = c = null != a ? new Uint32Array(a) : null != d ? new Uint32Array(d) : null != f ? new Uint32Array(f.__array) : null != h ? new Uint32Array(h) : null != k ? null == n ? new Uint32Array(k,
                    0) : new Uint32Array(k, 0, n) : null;
                n = k = h = f = d = null;
                this.hullTri = c = null != a ? new Uint32Array(a) : null != d ? new Uint32Array(d) : null != f ? new Uint32Array(f.__array) : null != h ? new Uint32Array(h) : null != k ? null == n ? new Uint32Array(k, 0) : new Uint32Array(k, 0, n) : null;
                c = this.hashSize;
                n = k = h = f = d = null;
                this.hullHash = c = null != c ? new Int32Array(c) : null != d ? new Int32Array(d) : null != f ? new Int32Array(f.__array) : null != h ? new Int32Array(h) : null != k ? null == n ? new Int32Array(k, 0) : new Int32Array(k, 0, n) : null;
                this.update()
            };
            g["com.watabou.geom.Delaunator"] =
                Qb;
            Qb.__name__ = "com.watabou.geom.Delaunator";
            Qb.pseudoAngle = function(a, b) {
                a /= Math.abs(a) + Math.abs(b);
                return (0 < b ? 3 - a : 1 + a) / 4
            };
            Qb.dist = function(a, b, c, d) {
                a -= c;
                b -= d;
                return a * a + b * b
            };
            Qb.orientIfSure = function(a, b, c, d, f, h) {
                d = (d - b) * (f - a);
                a = (c - a) * (h - b);
                return Math.abs(d - a) >= 3.3306690738754716E-16 * Math.abs(d + a) ? d - a : 0
            };
            Qb.orient = function(a, b, c, d, f, h) {
                var k = Qb.orientIfSure(a, b, c, d, f, h);
                if (0 != k) return 0 > k;
                k = Qb.orientIfSure(c, d, f, h, a, b);
                if (0 != k) return 0 > k;
                k = Qb.orientIfSure(f, h, a, b, c, d);
                return 0 != k ? 0 > k : !1
            };
            Qb.inCircle =
                function(a, b, c, d, f, h, k, n) {
                    a -= k;
                    b -= n;
                    c -= k;
                    d -= n;
                    f -= k;
                    h -= n;
                    n = c * c + d * d;
                    k = f * f + h * h;
                    return 0 > a * (d * k - n * h) - b * (c * k - n * f) + (a * a + b * b) * (c * h - d * f)
                };
            Qb.circumradius2 = function(a, b, c, d, f, h) {
                c -= a;
                d -= b;
                a = f - a;
                var k = h - b;
                b = c * c + d * d;
                h = a * a + k * k;
                f = .5 / (c * k - d * a);
                d = (k * b - d * h) * f;
                c = (c * h - a * b) * f;
                return d * d + c * c
            };
            Qb.circumcenter = function(a, b, c, d, f, h) {
                c -= a;
                d -= b;
                f -= a;
                h -= b;
                var k = c * c + d * d,
                    n = f * f + h * h,
                    p = .5 / (c * h - d * f);
                return new I(a + (h * k - d * n) * p, b + (c * n - f * k) * p)
            };
            Qb.triCircumcenter = function(a, b, c) {
                var d = a.x * a.x + a.y * a.y,
                    f = b.x * b.x + b.y * b.y,
                    h = c.x * c.x + c.y *
                    c.y,
                    k = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
                return new I(1 / k * (d * (b.y - c.y) + f * (c.y - a.y) + h * (a.y - b.y)), 1 / k * (d * (c.x - b.x) + f * (a.x - c.x) + h * (b.x - a.x)))
            };
            Qb.prototype = {
                update: function() {
                    for (var a = Infinity, b = Infinity, c = -Infinity, d = -Infinity, f = 0, h = this.points; f < h.length;) {
                        var k = h[f];
                        ++f;
                        k.x < a && (a = k.x);
                        k.y < b && (b = k.y);
                        k.x > c && (c = k.x);
                        k.y > d && (d = k.y)
                    }
                    c = (a + c) / 2;
                    var n = (b + d) / 2;
                    a = d = b = null;
                    var p = Infinity;
                    f = 0;
                    for (h = this.points; f < h.length;) {
                        k = h[f];
                        ++f;
                        var P = Qb.dist(k.x, k.y, c, n);
                        p > P && (p = P, b = k)
                    }
                    p = Infinity;
                    f = 0;
                    for (h =
                        this.points; f < h.length;) k = h[f], ++f, k != b && (P = Qb.dist(k.x, k.y, b.x, b.y), p > P && (p = P, d = k));
                    c = Infinity;
                    f = 0;
                    for (h = this.points; f < h.length;) k = h[f], ++f, k != b && k != d && (n = Qb.circumradius2(b.x, b.y, d.x, d.y, k.x, k.y), c > n && (c = n, a = k));
                    if (Qb.orient(b.x, b.y, d.x, d.y, a.x, a.y)) {
                        var g = d;
                        d = a;
                        a = g
                    }
                    this.center = Qb.circumcenter(b.x, b.y, d.x, d.y, a.x, a.y);
                    f = [];
                    h = 0;
                    for (P = this.points.length; h < P;) c = h++, f.push(c);
                    n = f;
                    f = [];
                    h = 0;
                    for (P = this.points; h < P.length;) k = P[h], ++h, f.push(I.distance(k, this.center));
                    var m = f;
                    n.sort(function(a, b) {
                        a = m[a] -
                            m[b];
                        return 0 == a ? 0 : 0 > a ? -1 : 1
                    });
                    f = [];
                    h = 0;
                    for (P = this.points.length; h < P;) c = h++, f.push(this.points[n[c]]);
                    this.points = f;
                    k = this.points.indexOf(b);
                    p = this.points.indexOf(d);
                    P = this.points.indexOf(a);
                    this.hullStart = k;
                    n = this.hullPrev[P] = p;
                    this.hullNext[k] = n;
                    n = this.hullPrev[k] = P;
                    this.hullNext[p] = n;
                    n = this.hullPrev[p] = k;
                    this.hullNext[P] = n;
                    this.hullTri[k] = 0;
                    this.hullTri[p] = 1;
                    this.hullTri[P] = 2;
                    f = 0;
                    for (h = this.hashSize; f < h;) c = f++, this.hullHash[c] = -1;
                    this.hullHash[this.hashKey(b)] = k;
                    this.hullHash[this.hashKey(d)] = p;
                    this.hullHash[this.hashKey(a)] = P;
                    this.trianglesLen = 0;
                    this.addTriangle(k, p, P, -1, -1, -1);
                    f = 0;
                    for (h = this.points; f < h.length;)
                        if (k = h[f], ++f, k != b && k != d && k != a && (n = k.x, p = k.y, !(Math.abs(n - Infinity) <= Qb.EPSILON && Math.abs(p - -Infinity) <= Qb.EPSILON))) {
                            c = this.points.indexOf(k);
                            var q = -1;
                            k = this.hashKey(k);
                            P = 0;
                            for (g = this.hashSize; P < g && (q = P++, q = this.hullHash[(k + q) % this.hashSize], -1 == q || q == this.hullNext[q]););
                            var u = this.hullPrev[q];
                            P = u;
                            for (q = this.hullNext[P]; !Qb.orient(n, p, this.points[P].x, this.points[P].y, this.points[q].x,
                                    this.points[q].y);) {
                                P = q;
                                if (P == u) {
                                    P = -1;
                                    break
                                }
                                q = this.hullNext[P]
                            }
                            if (-1 != P) {
                                g = this.addTriangle(P, c, this.hullNext[P], -1, -1, this.hullTri[P]);
                                this.hullTri[c] = this.legalize(g + 2);
                                this.hullTri[P] = g;
                                q = this.hullNext[P];
                                for (var r = this.hullNext[q]; Qb.orient(n, p, this.points[q].x, this.points[q].y, this.points[r].x, this.points[r].y);) g = this.addTriangle(q, c, r, this.hullTri[c], -1, this.hullTri[q]), this.hullTri[c] = this.legalize(g + 2), this.hullNext[q] = q, q = r, r = this.hullNext[q];
                                if (P == u)
                                    for (u = this.hullPrev[P]; Qb.orient(n, p,
                                            this.points[u].x, this.points[u].y, this.points[P].x, this.points[P].y);) g = this.addTriangle(u, c, P, -1, this.hullTri[P], this.hullTri[u]), this.legalize(g + 2), this.hullTri[u] = g, this.hullNext[P] = P, P = u, u = this.hullPrev[P];
                                this.hullStart = this.hullPrev[c] = P;
                                n = this.hullPrev[q] = c;
                                this.hullNext[P] = n;
                                this.hullNext[c] = q;
                                this.hullHash[k] = c;
                                this.hullHash[this.hashKey(this.points[P])] = P
                            }
                        } this.triangles = this.triangles.subarray(0, this.trianglesLen);
                    this.halfedges = this.halfedges.subarray(0, this.trianglesLen)
                },
                hashKey: function(a) {
                    return Math.floor(Qb.pseudoAngle(a.x -
                        this.center.x, a.y - this.center.y) * this.hashSize) % this.hashSize
                },
                legalize: function(a) {
                    for (var b = 0, c;;) {
                        var d = this.halfedges[a],
                            f = a - a % 3;
                        c = f + (a + 2) % 3;
                        if (-1 == d) {
                            if (0 == b) break;
                            a = Qb.EDGE_STACK[--b]
                        } else {
                            var h = d - d % 3,
                                k = h + (d + 2) % 3,
                                n = this.triangles[c],
                                p = this.triangles[a];
                            f = this.triangles[f + (a + 1) % 3];
                            var P = this.triangles[k];
                            if (Qb.inCircle(this.points[n].x, this.points[n].y, this.points[p].x, this.points[p].y, this.points[f].x, this.points[f].y, this.points[P].x, this.points[P].y)) {
                                this.triangles[a] = P;
                                this.triangles[d] =
                                    n;
                                n = this.halfedges[k];
                                if (-1 == n) {
                                    p = this.hullStart;
                                    do {
                                        if (this.hullTri[p] == k) {
                                            this.hullTri[p] = a;
                                            break
                                        }
                                        p = this.hullPrev[p]
                                    } while (p != this.hullStart)
                                }
                                this.link(a, n);
                                this.link(d, this.halfedges[c]);
                                this.link(c, k);
                                c = h + (d + 1) % 3;
                                b < Qb.EDGE_STACK.length && (Qb.EDGE_STACK[b++] = c)
                            } else {
                                if (0 == b) break;
                                a = Qb.EDGE_STACK[--b]
                            }
                        }
                    }
                    return c
                },
                addTriangle: function(a, b, c, d, f, h) {
                    var k = this.trianglesLen;
                    this.triangles[k] = a;
                    this.triangles[k + 1] = b;
                    this.triangles[k + 2] = c;
                    this.link(k, d);
                    this.link(k + 1, f);
                    this.link(k + 2, h);
                    this.trianglesLen +=
                        3;
                    return k
                },
                link: function(a, b) {
                    this.halfedges[a] = b; - 1 != b && (this.halfedges[b] = a)
                },
                getVoronoi: function() {
                    for (var a = new pa, b = this.triangles.length, c = [], d = 0, f = b / 3 | 0; d < f;) d++, c.push(null);
                    var h = c;
                    c = [];
                    d = 0;
                    for (f = this.points.length; d < f;) d++, c.push(!1);
                    var k = c;
                    c = 0;
                    for (d = b; c < d;) {
                        var n = c++;
                        b = this.triangles[2 == n % 3 ? n - 2 : n + 1];
                        if (!k[b]) {
                            k[b] = !0;
                            f = [];
                            var p = 0;
                            for (n = this.edgesAroundPoint(n); p < n.length;) {
                                var P = n[p];
                                ++p;
                                P = P / 3 | 0;
                                var g = h[P];
                                null == g && (g = h[P] = this.triangleCenter(P));
                                f.push(g)
                            }
                            a.set(this.points[b], f)
                        }
                    }
                    return a
                },
                triangleCenter: function(a) {
                    return Qb.triCircumcenter(this.points[this.triangles[3 * a]], this.points[this.triangles[3 * a + 1]], this.points[this.triangles[3 * a + 2]])
                },
                edgesAroundPoint: function(a) {
                    var b = [],
                        c = a;
                    do b.push(c), c = this.halfedges[2 == c % 3 ? c - 2 : c + 1]; while (-1 != c && c != a);
                    return b
                },
                __class__: Qb
            };
            var Ua = function() {};
            g["com.watabou.geom.EdgeChain"] = Ua;
            Ua.__name__ = "com.watabou.geom.EdgeChain";
            Ua.toPoly = function(a) {
                for (var b = [], c = 0; c < a.length;) {
                    var d = a[c];
                    ++c;
                    b.push(d.origin.point)
                }
                return b
            };
            Ua.toPolyline = function(a) {
                for (var b = [], c = 0; c < a.length;) {
                    var d = a[c];
                    ++c;
                    b.push(d.origin.point)
                }
                b.push(a[a.length - 1].next.origin.point);
                return b
            };
            Ua.assignData = function(a, b, c) {
                null == c && (c = !0);
                for (var d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    if (c || null == f.data) f.data = b;
                    null == f.twin || !c && null != f.twin.data || (f.twin.data = b)
                }
            };
            Ua.vertices = function(a, b) {
                null == b && (b = !1);
                for (var c = [], d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    c.push(f.origin)
                }
                b && c.push(a[a.length - 1].next.origin);
                return c
            };
            Ua.edgeByOrigin = function(a, b) {
                for (var c = 0; c < a.length;) {
                    var d = a[c];
                    ++c;
                    if (d.origin ==
                        b) return d
                }
                return null
            };
            Ua.indexByOrigin = function(a, b) {
                for (var c = 0, d = a.length; c < d;) {
                    var f = c++;
                    if (a[f].origin == b) return f
                }
                return -1
            };
            Ua.prev = function(a, b) {
                b = a.indexOf(b);
                return 0 < b ? a[b - 1] : a[a.length - 1]
            };
            var qa = function() {};
            g["com.watabou.geom.GeomUtils"] = qa;
            qa.__name__ = "com.watabou.geom.GeomUtils";
            qa.intersectLines = function(a, b, c, d, f, h, k, n) {
                var p = c * n - d * k;
                if (0 == p) return null;
                p = (d * (f - a) - c * (h - b)) / p;
                return new I(Math.abs(c) > Math.abs(d) ? (f - a + k * p) / c : (h - b + n * p) / d, p)
            };
            qa.lerp = function(a, b, c) {
                null == c && (c = .5);
                var d = a.x,
                    f = c;
                null == f && (f = .5);
                var h = a.y;
                a = c;
                null == a && (a = .5);
                return new I(d + (b.x - d) * f, h + (b.y - h) * a)
            };
            qa.converge = function(a, b, c, d) {
                var f = b.x - a.x,
                    h = b.y - a.y;
                a = b.x * a.y - b.y * a.x;
                return 1E-9 > Math.abs(f * c.y - h * c.x - a) ? 1E-9 > Math.abs(f * d.y - h * d.x - a) : !1
            };
            qa.barycentric = function(a, b, c, d) {
                var f = a.subtract(d),
                    h = b.subtract(d);
                d = c.subtract(d);
                a = (a.x - b.x) * (a.y - c.y) - (a.y - b.y) * (a.x - c.x);
                return new ch((h.x * d.y - h.y * d.x) / a, (d.x * f.y - d.y * f.x) / a, (f.x * h.y - f.y * h.x) / a)
            };
            qa.triArea = function(a, b, c) {
                return .5 * ((a.x - c.x) * (b.y - a.y) - (a.x -
                    b.x) * (c.y - a.y))
            };
            var bk = function() {
                this.nodes = []
            };
            g["com.watabou.geom.Graph"] = bk;
            bk.__name__ = "com.watabou.geom.Graph";
            bk.prototype = {
                add: function(a) {
                    this.nodes.push(a);
                    return a
                },
                aStar: function(a, b, c) {
                    c = null != c ? c.slice() : [];
                    var d = [a],
                        f = new pa,
                        h = new pa;
                    for (h.set(a, 0); 0 < d.length;) {
                        a = d.shift();
                        if (a == b) return this.buildPath(f, a);
                        N.remove(d, a);
                        c.push(a);
                        var k = h.h[a.__id__],
                            n = a.links,
                            p = n;
                        for (n = n.keys(); n.hasNext();) {
                            var P = n.next(),
                                g = p.get(P);
                            if (-1 == c.indexOf(P)) {
                                g = k + g;
                                if (-1 == d.indexOf(P)) d.push(P);
                                else if (g >=
                                    h.h[P.__id__]) continue;
                                f.set(P, a);
                                h.set(P, g)
                            }
                        }
                    }
                    return null
                },
                buildPath: function(a, b) {
                    for (var c = [b]; null != a.h.__keys__[b.__id__];) b = a.h[b.__id__], c.push(b);
                    return c
                },
                __class__: bk
            };
            var ck = function(a) {
                this.links = new pa;
                this.data = a
            };
            g["com.watabou.geom.Node"] = ck;
            ck.__name__ = "com.watabou.geom.Node";
            ck.prototype = {
                link: function(a, b, c) {
                    null == c && (c = !0);
                    null == b && (b = 1);
                    this.links.set(a, b);
                    c && a.links.set(this, b)
                },
                unlink: function(a, b) {
                    null == b && (b = !0);
                    this.links.remove(a);
                    b && a.links.remove(this)
                },
                unlinkAll: function() {
                    for (var a =
                            this.links.keys(); a.hasNext();) {
                        var b = a.next();
                        this.unlink(b)
                    }
                },
                __class__: ck
            };
            var dk = function(a, b, c, d) {
                null == d && (d = 0);
                this.width = a;
                this.height = b;
                this.dist = c;
                this.dist2 = c * c;
                this.cellSize = c / Math.sqrt(2);
                this.gridWidth = Math.ceil(this.width / this.cellSize);
                this.gridHeight = Math.ceil(this.height / this.cellSize);
                c = [];
                for (var f = 0, h = this.gridWidth * this.gridHeight; f < h;) f++, c.push(null);
                this.grid = c;
                this.points = [];
                this.queue = [];
                for (this.emit(new I(a * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647), b * ((C.seed = 48271 *
                        C.seed % 2147483647 | 0) / 2147483647))); this.step(););
                0 < d && this.uneven(d)
            };
            g["com.watabou.geom.PoissonPattern"] = dk;
            dk.__name__ = "com.watabou.geom.PoissonPattern";
            dk.prototype = {
                emit: function(a) {
                    this.points.push(a);
                    this.queue.push(a);
                    this.grid[Math.floor(a.y / this.cellSize) * this.gridWidth + Math.floor(a.x / this.cellSize)] = a
                },
                step: function() {
                    if (0 == this.queue.length) return !1;
                    for (var a = Z.random(this.queue), b = !1, c = 0; 50 > c;) {
                        c++;
                        var d = I.polar(this.dist * (1 + .1 * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647)), 6.28318530718 *
                            ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647));
                        d.x += a.x;
                        d.y += a.y;
                        this.warp(d);
                        this.validate(d) && (b = !0, this.emit(d))
                    }
                    b || N.remove(this.queue, a);
                    return 0 < this.queue.length
                },
                warp: function(a) {
                    0 > a.x ? a.x += this.width : a.x >= this.width && (a.x -= this.width);
                    0 > a.y ? a.y += this.height : a.y >= this.height && (a.y -= this.height)
                },
                validate: function(a) {
                    var b = a.x;
                    a = a.y;
                    for (var c = Math.floor(b / this.cellSize), d = Math.floor(a / this.cellSize), f = c - 2, h = c + 2 + 1, k = d - 2, n = d + 2 + 1; k < n;) {
                        d = k++;
                        d = (d + this.gridHeight) % this.gridHeight * this.gridWidth;
                        for (var p = f, P = h; p < P;) {
                            c = p++;
                            var g = this.grid[d + (c + this.gridWidth) % this.gridWidth];
                            if (null != g && (c = Math.abs(g.x - b), g = Math.abs(g.y - a), c > this.width - c && (c = this.width - c), g > this.height - g && (g = this.height - g), c * c + g * g < this.dist2)) return !1
                        }
                    }
                    return !0
                },
                uneven: function(a) {
                    if (0 != a)
                        for (var b = 0, c = this.points; b < c.length;) {
                            var d = c[b];
                            ++b;
                            var f = I.polar(this.dist * a * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647), 6.28318530718 * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647));
                            d.x += f.x;
                            d.y += f.y;
                            this.warp(d)
                        }
                },
                fill: function(a) {
                    var b =
                        a.getBounds(),
                        c = Math.floor(b.get_left() / this.width),
                        d = Math.floor(b.get_top() / this.height),
                        f = Math.ceil(b.get_right() / this.width);
                    b = Math.ceil(b.get_bottom() / this.height);
                    for (var h = []; d < b;)
                        for (var k = d++, n = c, p = f; n < p;)
                            for (var P = n++, g = a.validateRect(P * this.width, k * this.height, this.width, this.height), m = 0, q = this.points; m < q.length;) {
                                var u = q[m];
                                ++m;
                                if (0 != k || 0 != P) u = new I(u.x + P * this.width, u.y + k * this.height);
                                (g || a.validate(u)) && h.push(u)
                            }
                    return h
                },
                __class__: dk
            };
            var Xh = function() {};
            g["com.watabou.geom.IFillableShape"] =
                Xh;
            Xh.__name__ = "com.watabou.geom.IFillableShape";
            Xh.__isInterface__ = !0;
            Xh.prototype = {
                __class__: Xh
            };
            var Yh = function(a) {
                this.poly = a;
                this.rect = Gb.rect(a)
            };
            g["com.watabou.geom.FillablePoly"] = Yh;
            Yh.__name__ = "com.watabou.geom.FillablePoly";
            Yh.__interfaces__ = [Xh];
            Yh.prototype = {
                getBounds: function() {
                    return this.rect
                },
                validate: function(a) {
                    return Gb.containsPoint(this.poly, a)
                },
                validateRect: function(a, b, c, d) {
                    return !1
                },
                __class__: Yh
            };
            var hf = function(a, b) {
                this.start = a;
                this.end = b
            };
            g["com.watabou.geom.Segment"] = hf;
            hf.__name__ = "com.watabou.geom.Segment";
            hf.prototype = {
                __class__: hf
            };
            var jf = function(a, b) {
                null == b && (b = !1);
                this.height = 0;
                this.poly = a;
                for (var c = a.length, d = [], f = 0, h = c; f < h;) {
                    var k = f++;
                    d.push(new ek(a[k], a[(k + 1) % c]))
                }
                this.segments = d;
                this.leaves = new pa;
                d = [];
                f = 0;
                for (h = c; f < h;) {
                    k = f++;
                    var n = new Zh(a[k]);
                    this.leaves.set(a[k], n);
                    d.push(new dh(n, this.segments[k], this.segments[(k + c - 1) % c]))
                }
                this.ribs = d;
                this.bones = [];
                b && this.run()
            };
            g["com.watabou.geom.SkeletonBuilder"] = jf;
            jf.__name__ = "com.watabou.geom.SkeletonBuilder";
            jf.intersect = function(a, b) {
                return qa.intersectLines(a.a.point.x, a.a.point.y, a.slope.x, a.slope.y, b.a.point.x, b.a.point.y, b.slope.x, b.slope.y)
            };
            jf.prototype = {
                run: function() {
                    for (; this.step(););
                },
                step: function() {
                    if (2 >= this.ribs.length) return 2 == this.ribs.length && (this.root = this.ribs[0], this.root.b = this.ribs[1].a, this.root.b.parent = this.root, this.bones.push(this.root), this.ribs = []), !1;
                    for (var a = Infinity, b = null, c = null, d = null, f = 0, h = this.ribs; f < h.length;) {
                        var k = h[f];
                        ++f;
                        var n = Infinity,
                            p = null,
                            P = null,
                            g = k.right.lRib,
                            m = jf.intersect(k, g);
                        if (null != m && 0 <= m.x && 0 <= m.y) {
                            var q = m.x + k.a.height;
                            n > q && (n = q, P = m, p = g)
                        }
                        m = k.left.rRib;
                        g = jf.intersect(k, m);
                        null != g && 0 <= g.x && 0 <= g.y && n > g.x + k.a.height && (P = g, p = m);
                        null != P && (n = P.y + p.a.height, a > n && (a = n, b = k, c = p, d = P))
                    }
                    return null != d ? (this.height = a, a = b.a.point, f = b.slope, m = d.x, d = new I(a.x + f.x * m, a.y + f.y * m), this.merge(b, c, d), !0) : !1
                },
                merge: function(a, b, c) {
                    c = new Zh(c, this.height, a, b);
                    c = a.right == b.left ? new dh(c, a.left, b.right) : new dh(c, b.left, a.right);
                    this.ribs.push(c);
                    this.bones.push(a);
                    N.remove(this.ribs,
                        a);
                    this.bones.push(b);
                    N.remove(this.ribs, b);
                    return c
                },
                addGables: function() {
                    this.gables = [];
                    for (var a = 0, b = this.segments; a < b.length;) {
                        var c = b[a];
                        ++a;
                        var d = this.leaves.h[c.p0.__id__].parent,
                            f = this.leaves.h[c.p1.__id__].parent;
                        if (d.b == f.b) {
                            var h = d.b,
                                k = h.point;
                            d = (d == this.root ? f == h.child1 ? h.child2 : f == h.child2 ? h.child1 : null : f == this.root ? d == h.child1 ? h.child2 : d == h.child2 ? h.child1 : null : h.parent).slope;
                            h = qa.intersectLines(c.p0.x, c.p0.y, c.dir.x, c.dir.y, k.x, k.y, d.x, d.y);
                            null != h && 0 < h.x && h.x < c.len && (d = c.p0, f = c.dir,
                                h = h.x, d = new I(d.x + f.x * h, d.y + f.y * h), wd.set(k, d), this.gables.push(c))
                        }
                    }
                },
                getPath: function(a, b) {
                    if (a == b) return [a];
                    a = this.getPath2Root(a);
                    b = this.getPath2Root(b);
                    if (a[a.length - 1] == b[b.length - 1]) {
                        for (; a[a.length - 1] == b[b.length - 1];) a.pop(), b.pop();
                        a.push(a[a.length - 1].parent.b)
                    }
                    return a.concat(Z.revert(b))
                },
                getPath2Root: function(a) {
                    for (var b = [a]; this.root.a != a && this.root.b != a;) a = a.parent.b, b.push(a);
                    return b
                },
                __class__: jf
            };
            var dh = function(a, b, c) {
                this.a = a;
                this.a.parent = this;
                this.left = b;
                this.right = c;
                b.lRib =
                    this;
                c.rRib = this;
                a = c.dir;
                b = b.dir;
                c = a.x * b.x + a.y * b.y;
                .99999 < c ? this.slope = new I(-a.y, a.x) : (c = Math.sqrt((1 + c) / 2), this.slope = b.subtract(a), this.slope.normalize(1 / c), null == this.a.child1 && 0 > a.x * b.y - a.y * b.x && (a = this.slope, a.x *= -1, a.y *= -1))
            };
            g["com.watabou.geom.Rib"] = dh;
            dh.__name__ = "com.watabou.geom.Rib";
            dh.prototype = {
                __class__: dh
            };
            var Zh = function(a, b, c, d) {
                null == b && (b = 0);
                this.point = a;
                this.height = b;
                this.child1 = c;
                this.child2 = d;
                null != this.child1 && (this.child1.b = this, this.child2.b = this)
            };
            g["com.watabou.geom._SkeletonBuilder.Node"] =
                Zh;
            Zh.__name__ = "com.watabou.geom._SkeletonBuilder.Node";
            Zh.prototype = {
                __class__: Zh
            };
            var ek = function(a, b) {
                this.p0 = a;
                this.p1 = b;
                this.dir = b.subtract(a);
                this.len = this.dir.get_length();
                a = this.dir;
                b = 1 / this.len;
                a.x *= b;
                a.y *= b
            };
            g["com.watabou.geom._SkeletonBuilder.Segment"] = ek;
            ek.__name__ = "com.watabou.geom._SkeletonBuilder.Segment";
            ek.prototype = {
                __class__: ek
            };
            var xe = function() {};
            g["com.watabou.geom.Triangulation"] = xe;
            xe.__name__ = "com.watabou.geom.Triangulation";
            xe.earcut = function(a) {
                var b = [];
                xe.earcutLinked(xe.linkedList(a,
                    0, a.length), b);
                return b
            };
            xe.linkedList = function(a, b, c) {
                for (var d = null; b < c;) {
                    var f = b++;
                    d = new fk(f, a[f], d)
                }
                return d
            };
            xe.earcutLinked = function(a, b) {
                if (null != a)
                    for (var c = a; a.prev != a.next;) {
                        var d = a.prev,
                            f = a.next;
                        if (xe.isEar(a)) b.push([d.i, a.i, f.i]), a.remove(), c = a = f.next;
                        else if (a = f, a == c) break
                    }
            };
            xe.isEar = function(a) {
                var b = a.prev,
                    c = a.next,
                    d = b.p,
                    f = a.p,
                    h = c.p;
                if (0 <= (f.y - d.y) * (h.x - f.x) - (f.x - d.x) * (h.y - f.y)) return !1;
                for (var k = a.next.next; k != a.prev;) {
                    xe.pointInTriangle(b.p, a.p, c.p, k.p) ? (d = k.prev.p, f = k.p, h = k.next.p,
                        d = 0 <= (f.y - d.y) * (h.x - f.x) - (f.x - d.x) * (h.y - f.y)) : d = !1;
                    if (d) return !1;
                    k = k.next
                }
                return !0
            };
            xe.pointInTriangle = function(a, b, c, d) {
                var f = a.x;
                a = a.y;
                var h = b.x;
                b = b.y;
                var k = c.x;
                c = c.y;
                var n = d.x;
                d = d.y;
                return 0 <= (k - n) * (a - d) - (f - n) * (c - d) && 0 <= (f - n) * (b - d) - (h - n) * (a - d) ? 0 <= (h - n) * (c - d) - (k - n) * (b - d) : !1
            };
            var fk = function(a, b, c) {
                this.i = a;
                this.p = b;
                null == c ? (this.prev = this, this.next = this) : (this.next = c.next, this.prev = c, c.next.prev = this, c.next = this)
            };
            g["com.watabou.geom._Triangulation.Node"] = fk;
            fk.__name__ = "com.watabou.geom._Triangulation.Node";
            fk.prototype = {
                remove: function() {
                    this.next.prev = this.prev;
                    this.prev.next = this.next
                },
                __class__: fk
            };
            var kf = function() {};
            g["com.watabou.geom.polygons.PolyAccess"] = kf;
            kf.__name__ = "com.watabou.geom.polygons.PolyAccess";
            kf.longest = function(a) {
                for (var b = -1, c = 0, d = 0, f = a.length; d < f;) {
                    var h = d++,
                        k = I.distance(a[h], a[(h + 1) % a.length]);
                    c < k && (c = k, b = h)
                }
                return b
            };
            kf.isConvexVertexi = function(a, b) {
                var c = a.length,
                    d = a[(b + c - 1) % c],
                    f = a[b];
                a = a[(b + 1) % c];
                return 0 < (f.x - d.x) * (a.y - f.y) - (f.y - d.y) * (a.x - f.x)
            };
            kf.forEdge = function(a,
                b) {
                for (var c = a.length, d = 0; d < c;) {
                    var f = d++;
                    b(a[f], a[(f + 1) % c])
                }
            };
            var ye = function() {};
            g["com.watabou.geom.polygons.PolyBool"] = ye;
            ye.__name__ = "com.watabou.geom.polygons.PolyBool";
            ye.augmentPolygons = function(a, b) {
                for (var c = a.length, d = b.length, f = [], h = 0, k = c; h < k;) {
                    var n = h++;
                    f.push([])
                }
                var p = f;
                f = [];
                h = 0;
                for (k = d; h < k;) {
                    var P = h++;
                    f.push([])
                }
                var g = f;
                f = 0;
                for (h = c; f < h;) {
                    n = f++;
                    var m = a[n],
                        q = a[(n + 1) % c],
                        u = m.x,
                        r = m.y,
                        l = q.x - u,
                        D = q.y - r;
                    k = 0;
                    for (var x = d; k < x;) {
                        P = k++;
                        var w = b[P],
                            z = b[(P + 1) % d],
                            J = w.x;
                        w = w.y;
                        z = qa.intersectLines(u, r,
                            l, D, J, w, z.x - J, z.y - w);
                        null != z && 0 <= z.x && 1 >= z.x && 0 <= z.y && 1 >= z.y && (z = {
                            a: z.x,
                            b: z.y,
                            p: qa.lerp(m, q, z.x)
                        }, p[n].push(z), g[P].push(z))
                    }
                }
                P = [];
                f = 0;
                for (h = c; f < h;)
                    if (n = f++, P.push(a[n]), n = p[n], 0 < n.length)
                        for (n.sort(function(a, b) {
                                a = a.a - b.a;
                                return 0 == a ? 0 : 0 > a ? -1 : 1
                            }), k = 0; k < n.length;) z = n[k], ++k, P.push(z.p);
                a = [];
                f = 0;
                for (h = d; f < h;)
                    if (n = f++, a.push(b[n]), d = g[n], 0 < d.length)
                        for (d.sort(function(a, b) {
                                a = a.b - b.b;
                                return 0 == a ? 0 : 0 > a ? -1 : 1
                            }), k = 0; k < d.length;) z = d[k], ++k, a.push(z.p);
                return [P, a]
            };
            ye.and = function(a, b, c) {
                null == c && (c = !1);
                var d =
                    ye.augmentPolygons(a, b),
                    f = d[0],
                    h = d[1];
                if (f.length == a.length) return Gb.containsPoint(a, b[0]) ? c ? a : b : Gb.containsPoint(b, a[0], c) ? c ? null : a : c ? a : null;
                d = f;
                for (var k = h, n = [], p = -1, g = null, q = 0, m = f.length; q < m;) {
                    var u = q++;
                    g = f[u];
                    if (-1 == a.indexOf(f[u])) {
                        p = u;
                        break
                    }
                }
                a = qa.lerp(g, f[(p + 1) % f.length]);
                Gb.containsPoint(b, a, c) || (d = h, k = f, p = h.indexOf(g));
                for (;;) {
                    n.push(d[p]);
                    b = (p + 1) % d.length;
                    c = d[b];
                    if (c == n[0]) return n;
                    c = k.indexOf(c); - 1 != c ? (p = c, b = d, d = k, k = b) : p = b
                }
            };
            var Gb = function() {};
            g["com.watabou.geom.polygons.PolyBounds"] =
                Gb;
            Gb.__name__ = "com.watabou.geom.polygons.PolyBounds";
            Gb.rect = function(a) {
                for (var b = Infinity, c = Infinity, d = -Infinity, f = -Infinity, h = 0; h < a.length;) {
                    var k = a[h];
                    ++h;
                    var n = k.x;
                    k = k.y;
                    n < b && (b = n);
                    k < c && (c = k);
                    n > d && (d = n);
                    k > f && (f = k)
                }
                return new na(b, c, d - b, f - c)
            };
            Gb.aabb = function(a) {
                for (var b = Infinity, c = Infinity, d = -Infinity, f = -Infinity, h = 0; h < a.length;) {
                    var k = a[h];
                    ++h;
                    var n = k.x;
                    k = k.y;
                    n < b && (b = n);
                    k < c && (c = k);
                    n > d && (d = n);
                    k > f && (f = k)
                }
                return [new I(b, c), new I(d, c), new I(d, f), new I(b, f)]
            };
            Gb.obb = function(a) {
                a = Gb.convexHull(a);
                for (var b = Infinity, c = null, d = null, f = a.length, h = 0; h < f;) {
                    var k = h++,
                        n = a[k];
                    k = a[(k + 1) % f];
                    if (n.x != k.x || n.y != k.y) {
                        n = k.subtract(n);
                        n.normalize(1);
                        for (var p = n.x, g = -n.y, q = k = Infinity, m = -Infinity, u = -Infinity, r = 0; r < a.length;) {
                            var xb = a[r];
                            ++r;
                            var l = xb.x * p - xb.y * g;
                            xb = xb.y * p + xb.x * g;
                            l < k && (k = l);
                            xb < q && (q = xb);
                            l > m && (m = l);
                            xb > u && (u = xb)
                        }
                        p = (m - k) * (u - q);
                        b > p && (b = p, c = [new I(k, q), new I(m, q), new I(m, u), new I(k, u)], d = n)
                    }
                }
                Yc.asRotateYX(c, d.y, d.x);
                return c
            };
            Gb.lir = function(a, b) {
                var c = function(a, b, c, d, f, h, k) {
                        b = (b - f) / k;
                        return new I((d -
                            a + h * b) / c, b)
                    },
                    d = a.length,
                    f = a[b < a.length - 1 ? b + 1 : 0].subtract(a[b]);
                f = f.clone();
                f.normalize(1);
                var h = Yc.rotateYX(a, f.y, -f.x),
                    k = h[b];
                a = k.y;
                k = k.x;
                b = h[(b + 1) % h.length].x;
                for (var n = k - b, p = [], g = [], q = 0, m = d; q < m;) {
                    var u = q++,
                        r = h[u];
                    u = h[(u + 1) % d];
                    u.x > r.x && (u.y < r.y && u.x > b && p.push(new hf(r, u)), u.y > r.y && r.x < k && g.push(new hf(r, u)))
                }
                h = d = 0;
                r = a;
                for (q = u = 0; q < p.length;) {
                    var xb = p[q];
                    ++q;
                    m = xb.start;
                    var l = xb.end;
                    xb = c(b, a, n, m.x, m.y, xb.end.subtract(xb.start).x, xb.end.subtract(xb.start).y);
                    xb = b + xb.x * n;
                    var D = a,
                        x = 0,
                        w = 0;
                    l.x > k ? (x = k, w =
                        m.y + (l.y - m.y) / (l.x - m.x) * (k - m.x)) : (x = l.x, w = l.y);
                    l = Math.max(Math.max((xb + x) / 2, m.x), b);
                    xb = xb != x ? w + (D - w) * (l - x) / (xb - x) : w;
                    D = k;
                    for (m = 0; m < g.length;) x = g[m], ++m, x = c(b, xb, n, x.start.x, x.start.y, x.end.subtract(x.start).x, x.end.subtract(x.start).y), 0 <= x.x && 1 >= x.x && 0 <= x.y && 1 >= x.y && (x = b + n * x.x, D > x && (D = x));
                    m = (a - xb) * (D - l);
                    m > u && (d = D, h = l, r = xb, u = m)
                }
                for (q = 0; q < g.length;) {
                    xb = g[q];
                    ++q;
                    m = xb.start;
                    l = xb.end;
                    xb = c(b, a, n, m.x, m.y, xb.end.subtract(xb.start).x, xb.end.subtract(xb.start).y);
                    xb = b + xb.x * n;
                    D = a;
                    w = x = 0;
                    m.x < b ? (x = b, w = m.y + (l.y -
                        m.y) / (l.x - m.x) * (b - m.x)) : (x = m.x, w = m.y);
                    l = Math.min(Math.min((xb + x) / 2, l.x), k);
                    xb = xb != x ? w + (D - w) * (l - x) / (xb - x) : w;
                    D = b;
                    for (m = 0; m < p.length;) x = p[m], ++m, x = c(b, xb, n, x.start.x, x.start.y, x.end.subtract(x.start).x, x.end.subtract(x.start).y), 0 <= x.x && 1 >= x.x && 0 <= x.y && 1 >= x.y && (x = b + n * x.x, D < x && (D = x));
                    m = (a - xb) * (l - D);
                    m > u && (d = l, h = D, r = xb, u = m)
                }
                return Yc.rotateYX([new I(d, a), new I(h, a), new I(h, r), new I(d, r)], -f.y, -f.x)
            };
            Gb.lira = function(a) {
                for (var b = -Infinity, c = null, d = 0, f = a.length; d < f;) {
                    var h = d++;
                    h = Gb.lir(a, h);
                    var k = Sa.area(h);
                    b < k && (b = k, c = h)
                }
                return c
            };
            Gb.convexHull = function(a) {
                var b = a.length;
                if (3 > b) return null;
                if (3 == b) return a;
                var c = a[0],
                    d = a[1],
                    f = a[2];
                c = 0 < (d.x - c.x) * (f.y - c.y) - (f.x - c.x) * (d.y - c.y) ? [f, c, d, f] : [f, d, c, f];
                for (d = 3;;) {
                    if (d >= b) return c[0] == c[c.length - 1] && c.pop(), c;
                    f = a[d++];
                    for (var h = c.length;;) {
                        var k = c[0];
                        var n = c[1];
                        0 <= (k.x - f.x) * (n.y - f.y) - (n.x - f.x) * (k.y - f.y) ? (k = c[h - 2], n = c[h - 1], k = 0 <= (n.x - k.x) * (f.y - k.y) - (f.x - k.x) * (n.y - k.y)) : k = !1;
                        if (!k) break;
                        if (d >= b) return c[0] == c[c.length - 1] && c.pop(), c;
                        f = a[d++]
                    }
                    for (;;) {
                        2 <= c.length ?
                            (h = c[c.length - 2], k = c[c.length - 1], h = 0 > (k.x - h.x) * (f.y - h.y) - (f.x - h.x) * (k.y - h.y)) : h = !1;
                        if (!h) break;
                        c.pop()
                    }
                    for (c.push(f);;) {
                        h = c[0];
                        k = c[1];
                        if (!(0 > (h.x - f.x) * (k.y - f.y) - (k.x - f.x) * (h.y - f.y))) break;
                        c.shift()
                    }
                    c.unshift(f)
                }
            };
            Gb.containsPoint = function(a, b, c) {
                null == c && (c = !1);
                var d = b.x;
                b = b.y;
                for (var f = a.length, h = a[f - 1], k = 0; k < f;) {
                    var n = k++,
                        p = h;
                    h = a[n];
                    n = p.x;
                    p = p.y;
                    var g = h.x - n,
                        q = h.y - p;
                    if (0 != q) {
                        var m = (q * (d - n) - g * (b - p)) / q;
                        0 >= m && (n = Math.abs(g) > Math.abs(q) ? (d - n - m) / g : (b - p) / q, 0 <= n && 1 >= n && (c = !c))
                    }
                }
                return c
            };
            var Sa = function() {};
            g["com.watabou.geom.polygons.PolyCore"] = Sa;
            Sa.__name__ = "com.watabou.geom.polygons.PolyCore";
            Sa.area = function(a) {
                for (var b = a.length, c = a[b - 1], d = a[0], f = c.x * d.y - d.x * c.y, h = 1; h < b;) {
                    var k = h++;
                    c = d;
                    d = a[k];
                    f += c.x * d.y - d.x * c.y
                }
                return .5 * f
            };
            Sa.rectArea = function(a) {
                return I.distance(a[0], a[1]) * I.distance(a[1], a[2])
            };
            Sa.perimeter = function(a) {
                for (var b = a.length, c = a[b - 1], d = a[0], f = I.distance(c, d), h = 1; h < b;) {
                    var k = h++;
                    c = d;
                    d = a[k];
                    f += I.distance(c, d)
                }
                return f
            };
            Sa.$length = function(a) {
                for (var b = 0, c = 1, d = a.length; c < d;) {
                    var f =
                        c++;
                    b += I.distance(a[f - 1], a[f])
                }
                return b
            };
            Sa.center = function(a) {
                for (var b = a.length, c = a[0].clone(), d = 1; d < b;) {
                    var f = d++;
                    f = a[f];
                    c.x += f.x;
                    c.y += f.y
                }
                a = 1 / b;
                c.x *= a;
                c.y *= a;
                return c
            };
            Sa.centroid = function(a) {
                if (1 == a.length) return a[0];
                for (var b = 0, c = 0, d = 0, f = a[a.length - 1], h = 0, k = a.length; h < k;) {
                    var n = h++,
                        p = f;
                    f = a[n];
                    n = p.x * f.y - p.y * f.x;
                    d += n;
                    b += (p.x + f.x) * n;
                    c += (p.y + f.y) * n
                }
                a = 1 / (3 * d);
                return new I(a * b, a * c)
            };
            Sa.compactness = function(a) {
                var b = Sa.perimeter(a);
                return 4 * Math.PI * Sa.area(a) / (b * b)
            };
            Sa.simplifyClosed = function(a) {
                for (var b = -1, c = Infinity, d = a.length, f = a[d - 2], h = a[d - 1], k = 0; k < d;) {
                    var n = k++,
                        p = f;
                    f = h;
                    h = a[n];
                    p = Math.abs(p.x * (f.y - h.y) + f.x * (h.y - p.y) + h.x * (p.y - f.y));
                    c > p && (c = p, b = 0 == n ? d - 1 : n - 1)
                }
                a.splice(b, 1)
            };
            Sa.set = function(a, b) {
                for (var c = 0, d = a.length; c < d;) {
                    var f = c++;
                    wd.set(a[f], b[f])
                }
            };
            var Qd = function() {};
            g["com.watabou.geom.polygons.PolyCreate"] = Qd;
            Qd.__name__ = "com.watabou.geom.polygons.PolyCreate";
            Qd.rect = function(a, b) {
                a /= 2;
                b /= 2;
                return [new I(-a, -b), new I(a, -b), new I(a, b), new I(-a, b)]
            };
            Qd.regular = function(a, b, c) {
                null == c && (c = 0);
                for (var d = [], f = 0; f < a;) {
                    var h = f++;
                    d.push(I.polar(b, c + 2 * Math.PI * h / a))
                }
                return d
            };
            Qd.stripe = function(a, b, c) {
                null == c && (c = 1);
                var d = b / 2,
                    f = a.length;
                b = [];
                var h = [],
                    k = a[0],
                    n = a[1],
                    p = n.subtract(k);
                p.normalize(1);
                if (0 < c) {
                    var g = new I(p.x * d, p.y * d),
                        q = new I(-g.y, g.x);
                    k = k.subtract(new I(g.x * c, g.y * c));
                    b.unshift(k.subtract(q));
                    h.unshift(k.add(q))
                } else {
                    var m = new I(p.x * d, p.y * d);
                    q = new I(-m.y, m.x);
                    b.unshift(k.subtract(q));
                    h.unshift(k.add(q))
                }
                q = 1;
                for (--f; q < f;) g = q++, k = n, n = a[g + 1], m = p, p = n.subtract(k), p.normalize(1), g = m.x * p.x + m.y * p.y, m =
                    m.add(p), m = new I(-m.y, m.x), m.normalize(d * Math.sqrt(2 / (1 + g))), b.push(k.subtract(m)), h.push(k.add(m));
                0 < c ? (g = new I(p.x * d, p.y * d), q = new I(-g.y, g.x), a = n.add(new I(g.x * c, g.y * c)), b.push(a.subtract(q)), h.push(a.add(q))) : (m = new I(p.x * d, p.y * d), q = new I(-m.y, m.x), b.push(n.subtract(q)), h.push(n.add(q)));
                return b.concat(Z.revert(h))
            };
            var gd = function() {};
            g["com.watabou.geom.polygons.PolyCut"] = gd;
            gd.__name__ = "com.watabou.geom.polygons.PolyCut";
            gd.pierce = function(a, b, c) {
                for (var d = b.x, f = b.y, h = c.x - d, k = c.y - f, n = a.length,
                        p = [], g = 0, q = n; g < q;) {
                    var m = g++,
                        u = a[m];
                    m = a[(m + 1) % n];
                    var r = u.x;
                    u = u.y;
                    u = qa.intersectLines(d, f, h, k, r, u, m.x - r, m.y - u);
                    null != u && 0 <= u.y && 1 >= u.y && p.push(u.x)
                }
                p.sort(function(a, b) {
                    a -= b;
                    return 0 == a ? 0 : 0 > a ? -1 : 1
                });
                g = [];
                for (q = 0; q < p.length;) u = p[q], ++q, g.push(qa.lerp(b, c, u));
                return g
            };
            gd.cut = function(a, b, c, d, f) {
                null == f && (f = 0);
                null == d && (d = 0);
                var h = b.x,
                    k = b.y,
                    n = c.x - h,
                    p = c.y - k,
                    g = a.length,
                    q = 0,
                    m = 0,
                    u = 0,
                    r = 0;
                q = [];
                u = new pa;
                for (m = 0; m < g;) {
                    r = m++;
                    var l = a[r],
                        D = a[(r + 1) % g],
                        x = l.x;
                    l = l.y;
                    D = qa.intersectLines(h, k, n, p, x, l, D.x - x, D.y - l);
                    null !=
                        D && 0 <= D.y && 1 >= D.y && (q.push(D), u.set(D, r))
                }
                return 2 <= q.length ? (q.sort(function(a, b) {
                    a = a.x - b.x;
                    return 0 == a ? 0 : 0 > a ? -1 : 1
                }), m = q[0], r = q[1], q = u.h[m.__id__], u = u.h[r.__id__], 0 == f ? (h = qa.lerp(b, c, m.x), c = qa.lerp(b, c, r.x), b = q < u ? a.slice(q + 1, u + 1) : a.slice(q + 1).concat(a.slice(0, u + 1)), b.unshift(h), b.push(c), f = q < u ? a.slice(u + 1).concat(a.slice(0, q + 1)) : a.slice(u + 1, q + 1), f.unshift(c), f.push(h)) : (h = m.y < f ? a[q] : m.y > 1 - f ? a[(q + 1) % g] : qa.lerp(b, c, m.x), c = r.y < f ? a[u] : r.y > 1 - f ? a[(u + 1) % g] : qa.lerp(b, c, r.x), b = q < u ? a.slice(q + 1, u + 1) : a.slice(q +
                    1).concat(a.slice(0, u + 1)), h != b[0] && b.unshift(h), c != b[b.length - 1] && b.push(c), f = q < u ? a.slice(u + 1).concat(a.slice(0, q + 1)) : a.slice(u + 1, q + 1), c != f[0] && f.unshift(c), h != f[f.length - 1] && f.push(h)), 0 < d && (b = gd.peel(b, c, d / 2), f = gd.peel(f, h, d / 2)), a = a[q < a.length - 1 ? q + 1 : 0].subtract(a[q]), 0 < n * a.y - p * a.x ? [b, f] : [f, b]) : [a]
            };
            gd.peel = function(a, b, c) {
                var d = a.indexOf(b);
                d = a[d == a.length - 1 ? 0 : d + 1];
                var f = d.subtract(b);
                f = new I(-f.y, f.x);
                null == c && (c = 1);
                f = f.clone();
                f.normalize(c);
                c = f;
                return gd.cut(a, b.add(c), d.add(c))[0]
            };
            gd.shrink =
                function(a, b) {
                    for (var c = a.slice(), d = a.length, f = 0; f < d;) {
                        var h = f++,
                            k = b[h];
                        if (0 < k) {
                            var n = a[h];
                            h = a[(h + 1) % d];
                            var p = h.subtract(n);
                            p = new I(-p.y, p.x);
                            null == k && (k = 1);
                            p = p.clone();
                            p.normalize(k);
                            k = p;
                            c = gd.cut(c, n.add(k), h.add(k))[0]
                        }
                    }
                    return c
                };
            gd.shrinkEq = function(a, b) {
                for (var c = [], d = 0; d < a.length;) ++d, c.push(b);
                return gd.shrink(a, c)
            };
            var Yc = function() {};
            g["com.watabou.geom.polygons.PolyTransform"] = Yc;
            Yc.__name__ = "com.watabou.geom.polygons.PolyTransform";
            Yc.translate = function(a, b, c) {
                for (var d = [], f = 0; f < a.length;) {
                    var h =
                        a[f];
                    ++f;
                    d.push(new I(h.x + b, h.y + c))
                }
                return d
            };
            Yc.asTranslate = function(a, b, c) {
                for (var d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    f.x += b;
                    f.y += c
                }
            };
            Yc.asAdd = function(a, b) {
                Yc.asTranslate(a, b.x, b.y)
            };
            Yc.rotateYX = function(a, b, c) {
                for (var d = [], f = 0; f < a.length;) {
                    var h = a[f];
                    ++f;
                    d.push(new I(h.x * c - h.y * b, h.y * c + h.x * b))
                }
                return d
            };
            Yc.asRotateYX = function(a, b, c) {
                for (var d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    f.setTo(f.x * c - f.y * b, f.y * c + f.x * b)
                }
            };
            var Wk = function() {};
