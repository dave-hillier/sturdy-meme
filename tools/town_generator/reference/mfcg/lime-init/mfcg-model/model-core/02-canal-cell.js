/*
 * model-core/02-canal-cell.js
 * Part 2/8: Canal and Cell
 * Contains: Canal, Cell classes
 */
            g["com.watabou.mfcg.model.Canal"] = yb;
            yb.__name__ = "com.watabou.mfcg.model.Canal";
            yb.createRiver = function(a) {
                yb.buildTopology(a);
                var b = 0 < a.shoreE.length ? yb.deltaRiver(a) :
                    yb.regularRiver(a);
                if (null != b) return new yb(a, b);
                throw new Vb("Unable to build a canal!");
            };
            yb.regularRiver = function(a) {
                for (var b = Ua.vertices(a.horizonE), c = [], d = 0, f = b; d < f.length;) {
                    var h = f[d];
                    ++d;
                    1 < a.cellsByVertex(h).length && c.push(h)
                }
                for (b = c; 1 < b.length;) {
                    var k = Z.random(b),
                        n = null;
                    d = Infinity;
                    for (c = 0; c < b.length;) {
                        h = b[c];
                        ++c;
                        f = k.point;
                        var p = h.point;
                        p = p.clone();
                        p.normalize(1);
                        f = f.x * p.x + f.y * p.y;
                        d > f && (d = f, n = h)
                    }
                    h = Z.random(a.dcel.vertices.h[a.center.__id__].edges).next.origin;
                    c = yb.topology.buildPath(n, h);
                    h = null != c ? yb.topology.buildPath(h, k) : null;
                    if (null != c && null != h)
                        for (d = 0, f = h.length; d < f;) {
                            p = d++;
                            var g = c.indexOf(h[p]);
                            if (-1 != g) {
                                c = a.dcel.vertices2chain(h.slice(0, p).concat(c.slice(g)));
                                if (yb.validateCourse(c)) return c;
                                break
                            }
                        }
                    hb.trace("discard", {
                        fileName: "Source/com/watabou/mfcg/model/Canal.hx",
                        lineNumber: 216,
                        className: "com.watabou.mfcg.model.Canal",
                        methodName: "regularRiver"
                    });
                    N.remove(b, k);
                    N.remove(b, n)
                }
                return null
            };
            yb.deltaRiver = function(a) {
                for (var b = [], c = 1, d = a.shoreE.length - 1; c < d;) {
                    var f = c++,
                        h = a.shoreE[f].origin;
                    1 < Z.count(a.cellsByVertex(h), function(a) {
                        return !a.waterbody
                    }) && b.push(f)
                }
                b = Z.sortBy(b, function(b) {
                    return a.shoreE[b].origin.point.get_length()
                });
                f = Ua.vertices(Z.difference(a.earthEdgeE, a.shoreE));
                c = [];
                for (d = 0; d < f.length;) h = f[d], ++d, 1 < a.cellsByVertex(h).length && c.push(h);
                for (f = c; 0 < b.length;) {
                    c = b.shift();
                    d = a.shoreE[c].origin;
                    c = a.shoreE[c + 1].origin.point.subtract(a.shoreE[c - 1].origin.point);
                    c = c.clone();
                    c.normalize(1);
                    var k = new I(-c.y, c.x),
                        n = null,
                        p = -Infinity;
                    for (c = 0; c < f.length;) {
                        h = f[c];
                        ++c;
                        var g = h.point.subtract(d.point);
                        g = g.clone();
                        g.normalize(1);
                        g = k.x * g.x + k.y * g.y;
                        p < g && (p = g, n = h)
                    }
                    c = yb.topology.buildPath(n, d);
                    if (null != c && (c = a.dcel.vertices2chain(c), yb.validateCourse(c))) return c;
                    hb.trace("discard", {
                        fileName: "Source/com/watabou/mfcg/model/Canal.hx",
                        lineNumber: 268,
                        className: "com.watabou.mfcg.model.Canal",
                        methodName: "deltaRiver"
                    })
                }
                return null
            };
            yb.validateCourse = function(a) {
                if (null == a || a.length < yb.model.earthEdge.length / 5) return !1;
                for (var b = 1, c = a.length - 1; b < c;) {
                    var d = b++;
                    if (null != Ua.edgeByOrigin(yb.model.shoreE, a[d].origin)) return !1
                }
                if (null !=
                    yb.model.wall) {
                    var f = yb.model.wall.edges;
                    b = 0;
                    for (c = f.length; b < c;) {
                        d = b++;
                        var h = f[d];
                        d = Ua.indexByOrigin(a, h.origin);
                        if (0 < d && d < a.length - 1 && !yb.intersect(f, h, a, a[d])) return !1
                    }
                }
                b = 0;
                for (c = yb.model.arteries; b < c.length;) {
                    f = c[b];
                    ++b;
                    h = 1;
                    for (var k = f.length - 1; h < k;) {
                        d = h++;
                        var n = f[d];
                        d = Ua.indexByOrigin(a, n.origin);
                        if (0 < d && d < a.length - 1 && !yb.intersect(f, n, a, a[d])) return !1
                    }
                }
                return !0
            };
            yb.intersect = function(a, b, c, d) {
                a = Ua.prev(a, b);
                c = Ua.prev(c, d);
                do a = a.next.twin; while (a != c && a != b.twin && a != d.twin);
                if (a == b.twin) return !1;
                do a = a.next.twin; while (a != c && a != b.twin && a != d.twin);
                return a == b.twin ? !0 : !1
            };
            yb.buildTopology = function(a) {
                if (a.cells != yb.patches) {
                    yb.model = a;
                    yb.patches = a.cells;
                    for (var b = [], c = 0, d = a.cells; c < d.length;) {
                        var f = d[c];
                        ++c;
                        f.waterbody || b.push(f)
                    }
                    yb.topology = new gh(b);
                    null != a.wall && yb.topology.excludePolygon(a.wall.edges);
                    null != a.citadel && yb.topology.excludePoints(Ua.vertices(va.__cast(a.citadel.ward, xd).wall.edges));
                    yb.topology.excludePoints(a.gates);
                    b = 0;
                    for (c = a.arteries; b < c.length;) a = c[b], ++b, yb.topology.excludePolygon(a)
                }
            };
            yb.prototype = {
                updateState: function() {
                    this.gates = new pa;
                    if (null != yb.model.wall)
                        for (var a = 0, b = yb.model.wall.edges; a < b.length;) {
                            var c = b[a];
                            ++a;
                            var d = c.origin,
                                f = Ua.indexByOrigin(this.course, d);
                            0 < f && f < this.course.length - 1 && (yb.model.wall.addWatergate(d, this), this.gates.set(d, yb.model.wall))
                        }
                    this.bridges = new pa;
                    a = 0;
                    for (b = yb.model.arteries; a < b.length;) {
                        c = b[a];
                        ++a;
                        var h = 0;
                        for (d = c.length; h < d;) {
                            var k = h++,
                                n = c[k];
                            f = Ua.indexByOrigin(this.course, n.origin);
                            0 < f && f < this.course.length - 1 && (0 == k ? this.bridges.set(n.origin,
                                c) : yb.intersect(c, n, this.course, this.course[f]) && this.bridges.set(n.origin, c))
                        }
                    }
                    n = yb.model.inner;
                    c = [];
                    a = 2;
                    for (b = this.course.length - 1; a < b;) f = a++, f = this.course[f], -1 == n.indexOf(f.face.data) && -1 == n.indexOf(f.twin.face.data) || Z.add(c, f.origin);
                    a = [];
                    for (b = this.gates.keys(); b.hasNext();) n = b.next(), a.push(n);
                    Z.removeAll(c, a);
                    f = c.length;
                    this.rural = 0 == f;
                    this.width = (3 + yb.model.inner.length / 5) * (.8 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * .4) * (this.rural ? 1.5 : 1);
                    if (!this.rural) {
                        h = 0;
                        b = n = this.bridges;
                        for (d =
                            n.keys(); d.hasNext();) n = d.next(), b.get(n), -1 != c.indexOf(n) && ++h;
                        a = [];
                        b = n = this.bridges;
                        for (d = n.keys(); d.hasNext();) n = d.next(), b.get(n), a.push(n);
                        for (Z.removeAll(c, a);;) {
                            a = 1 - 2 * h / f;
                            null == a && (a = .5);
                            if (!((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < a)) break;
                            a = [];
                            for (b = 0; b < c.length;) n = c[b], ++b, a.push(1 / n.edges.length);
                            n = Z.weighted(c, a);
                            d = null;
                            this.bridges.set(n, d);
                            N.remove(c, n);
                            ++h
                        }
                    }
                    Ua.assignData(this.course, Tc.CANAL)
                },
                __class__: yb
            };
            var ci = function(a) {
                this.seed = -1;
                this.district = null;
                this.face = a;
                this.shape =
                    a.getPoly();
                a.data = this;
                this.waterbody = this.withinWalls = this.withinCity = !1;
                this.seed = C.seed = 48271 * C.seed % 2147483647 | 0
            };
            g["com.watabou.mfcg.model.Cell"] = ci;
            ci.__name__ = "com.watabou.mfcg.model.Cell";
            ci.prototype = {
                bordersInside: function(a) {
                    for (var b = 0; b < a.length;) {
                        var c = a[b];
                        ++b;
                        if (c.face == this.face) return !0
                    }
                    return !1
                },
                isRerollable: function() {
                    return null != this.view ? null != this.view.parent : !1
                },
                reroll: function() {
                    var a = null != this.ward.group ? this.ward.group.core.data : this;
                    a != this ? a.reroll() : this.isRerollable() &&
                        (this.seed = C.seed, this.ward.createGeometry(), this.view.draw(), ia.map.updateRoads(), ia.map.updateTrees(), Bb.geometryChanged.dispatch(this))
                },
                __class__: ci
            };
            var Tc = y["com.watabou.mfcg.model.Edge"] = {
                __ename__: "com.watabou.mfcg.model.Edge",
                __constructs__: null,
                HORIZON: {
                    _hx_name: "HORIZON",
                    _hx_index: 0,
                    __enum__: "com.watabou.mfcg.model.Edge",
                    toString: r
                },
                COAST: {
                    _hx_name: "COAST",
                    _hx_index: 1,
                    __enum__: "com.watabou.mfcg.model.Edge",
                    toString: r
                },
                ROAD: {
                    _hx_name: "ROAD",
                    _hx_index: 2,
                    __enum__: "com.watabou.mfcg.model.Edge",
                    toString: r
                },
                WALL: {
                    _hx_name: "WALL",
                    _hx_index: 3,
                    __enum__: "com.watabou.mfcg.model.Edge",
                    toString: r
                },
                CANAL: {
                    _hx_name: "CANAL",
                    _hx_index: 4,
                    __enum__: "com.watabou.mfcg.model.Edge",
                    toString: r
                }
            };
            Tc.__constructs__ = [Tc.HORIZON, Tc.COAST, Tc.ROAD, Tc.WALL, Tc.CANAL];
            var Ub = function(a) {
                this.bounds = new na;
                this.bp = a;
                var b = a.size,
                    c = a.seed;
                if (0 != b) {
                    0 < c && C.reset(c); - 1 == b && (b = Ub.nextSize);
                    this.nPatches = b;
                    hb.trace(">> seed:" + C.seed + " size:" + b, {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 124,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "new"
                    });
                    c = Ub.sizes.h;
                    for (var d = Object.keys(c), f = d.length, h = 0; h < f;) {
                        var k = c[d[h++]];
                        if (b >= k.min && b < k.max) {
                            Ub.nextSize = k.min + Math.random() * (k.max - k.min) | 0;
                            break
                        }
                    }
                    this.citadelNeeded = a.citadel;
                    this.stadtburgNeeded = a.inner;
                    this.plazaNeeded = a.plaza;
                    this.templeNeeded = a.temple;
                    this.wallsNeeded = a.walls;
                    this.shantyNeeded = a.shanty;
                    this.riverNeeded = a.river;
                    this.coastNeeded = a.coast;
                    this.maxDocks = (Math.sqrt(b / 2) | 0) + (this.riverNeeded ? 2 : 0);
                    do try {
                        hb.trace("--\x3e> seed:" + C.seed + " size:" + b, {
                            fileName: "Source/com/watabou/mfcg/model/City.hx",
                            lineNumber: 151,
                            className: "com.watabou.mfcg.model.City",
                            methodName: "new"
                        }), this.build(), Ub.instance = this
                    } catch (n) {
                        c = X.caught(n), hb.trace("*** " + H.string(c), {
                            fileName: "Source/com/watabou/mfcg/model/City.hx",
                            lineNumber: 156,
                            className: "com.watabou.mfcg.model.City",
                            methodName: "new"
                        }), Ub.instance = null
                    }
                    while (null == Ub.instance);
                    a.updateURL();
                    Bb.newModel.dispatch(this)
                }
            };
