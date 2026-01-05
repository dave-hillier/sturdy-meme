/*
 * model-core/03-city.js
 * Part 3/8: City
 * Contains: City class (main city generation)
 */
            g["com.watabou.mfcg.model.City"] = Ub;
            Ub.__name__ = "com.watabou.mfcg.model.City";
            Ub.prototype = {
                rerollName: function() {
                    return Ma.cityName(this)
                },
                setName: function(a, b) {
                    this.bp.name = this.name = a;
                    this.bp.updateURL();
                    Bb.titleChanged.dispatch(this.name)
                },
                build: function() {
                    this.streets = [];
                    this.roads = [];
                    this.walls = [];
                    this.landmarks = [];
                    this.north = 0;
                    hb.trace("buildPatches " + id.measure(l(this, this.buildPatches)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 183,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("optimizeJunctions " + id.measure(l(this, this.optimizeJunctions)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 184,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("buildDomains " + id.measure(l(this, this.buildDomains)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 185,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("buildWalls " + id.measure(l(this, this.buildWalls)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 186,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("buildStreets " + id.measure(l(this, this.buildStreets)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 187,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("buildCanals " + id.measure(l(this, this.buildCanals)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 188,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("createWards " + id.measure(l(this, this.createWards)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 189,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    hb.trace("buildCityTowers " +
                        id.measure(l(this, this.buildCityTowers)), {
                            fileName: "Source/com/watabou/mfcg/model/City.hx",
                            lineNumber: 190,
                            className: "com.watabou.mfcg.model.City",
                            methodName: "build"
                        });
                    hb.trace("buildGeometry " + id.measure(l(this, this.buildGeometry)), {
                        fileName: "Source/com/watabou/mfcg/model/City.hx",
                        lineNumber: 191,
                        className: "com.watabou.mfcg.model.City",
                        methodName: "build"
                    });
                    this.updateDimensions()
                },
                buildPatches: function() {
                    for (var a = this, b = 0, c = [new I], d = 2 * Math.PI * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647), f = 1, h =
                            8 * this.nPatches; f < h;) {
                        var k = f++,
                            n = 10 + k * (2 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647);
                        k = d + 5 * Math.sqrt(k);
                        c.push(I.polar(n, k));
                        b < n && (b = n)
                    }
                    this.plazaNeeded && (C.save(), f = 8 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 8, h = f * (1 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647), b = Math.max(b, h), c[1] = I.polar(f, d), c[2] = I.polar(h, d + Math.PI / 2), c[3] = I.polar(f, d + Math.PI), c[4] = I.polar(h, d + 3 * Math.PI / 2), C.restore());
                    h = (new Qb(c.concat(Qd.regular(6, 2 * b)))).getVoronoi();
                    for (f = h.keys(); f.hasNext();) c = f.next(), n = h.get(c),
                        Z.some(n, function(a) {
                            return a.get_length() > b
                        }) && h.remove(c);
                    this.cells = [];
                    this.inner = [];
                    f = [];
                    for (n = h.iterator(); n.hasNext();) h = n.next(), f.push(h);
                    this.dcel = new Ic(f);
                    var p = new pa;
                    f = 0;
                    for (h = this.dcel.faces; f < h.length;) {
                        var g = h[f];
                        ++f;
                        c = new ci(g);
                        n = Sa.centroid(c.shape);
                        p.set(c, n);
                        this.cells.push(c)
                    }
                    this.cells = Z.sortBy(this.cells, function(a) {
                        a = p.h[a.__id__];
                        return a.x * a.x + a.y * a.y
                    });
                    if (this.coastNeeded) {
                        C.save();
                        d = pg.fractal(6);
                        f = 20 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 40;
                        k = .3 * b * (((C.seed = 48271 *
                            C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2 - 1);
                        n = b * (.2 + Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1));
                        g = this.bp.coastDir;
                        isNaN(g) && (this.bp.coastDir = Math.floor((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 20) / 10);
                        C.restore();
                        h = this.bp.coastDir * Math.PI;
                        var q = Math.cos(h),
                            m = Math.sin(h);
                        g = new I(n + f, k);
                        f = 0;
                        for (h = this.cells; f < h.length;) {
                            c = h[f];
                            ++f;
                            var u = p.h[c.__id__],
                                r = new I(u.x * q - u.y * m, u.y * q + u.x * m);
                            u = I.distance(g, r) - n;
                            r.x > g.x && (u = Math.min(u, Math.abs(r.y - k) - n));
                            r = d.get((r.x + b) / (2 * b), (r.y + b) / (2 * b)) * n * Math.sqrt(r.get_length() / b);
                            0 > u + r && (c.waterbody = !0)
                        }
                    }
                    f = n = 0;
                    for (h = this.cells; f < h.length && (c = h[f], ++f, c.waterbody || (c.withinCity = !0, c.withinWalls = this.wallsNeeded, this.inner.push(c), !(++n > this.nPatches))););
                    this.center = Z.min(this.inner[0].shape, function(a) {
                        return a.x * a.x + a.y *
                            a.y
                    });
                    this.plazaNeeded && new he(this, this.plaza = this.inner[0]);
                    if (this.citadelNeeded) {
                        if (this.stadtburgNeeded) {
                            f = [];
                            h = 0;
                            for (c = this.inner; h < c.length;) n = c[h], ++h,
                                function(b) {
                                    for (var c = b.face.halfEdge, d = c, f = !0; f;) {
                                        var h = d;
                                        d = d.next;
                                        f = d != c;
                                        var k = 0;
                                        for (h = a.cellsByVertex(h.origin); k < h.length;) {
                                            var n = h[k];
                                            ++k;
                                            if (n != b && !n.waterbody && !n.withinCity) return !1
                                        }
                                    }
                                    return !0
                                }(n) && f.push(n);
                            N.remove(f, this.plaza);
                            0 < f.length ? f = Z.random(f) : (hb.trace("Unable to build an uraban castle!", {
                                fileName: "Source/com/watabou/mfcg/model/City.hx",
                                lineNumber: 332,
                                className: "com.watabou.mfcg.model.City",
                                methodName: "buildPatches"
                            }), k = this.inner, f = this.citadel = k[k.length - 1]);
                            this.citadel = f
                        } else k = this.inner, this.citadel = k[k.length - 1];
                        this.citadel.withinCity = !0;
                        this.citadel.withinWalls = !0;
                        N.remove(this.inner, this.citadel)
                    }
                },
                optimizeJunctions: function() {
                    var a = 3 * pc.LTOWER_RADIUS,
                        b = this.citadel;
                    for (b = null != b ? b.shape : null;;) {
                        for (var c = !1, d = 0, f = this.dcel.faces; d < f.length;) {
                            var h = f[d];
                            ++d;
                            var k = h.data.shape;
                            if (!(4 >= k.length)) {
                                var n = Sa.perimeter(k);
                                k =
                                    Math.max(a, n / k.length / 3);
                                n = h = h.halfEdge;
                                for (var p = !0; p;) {
                                    var g = n;
                                    n = n.next;
                                    p = n != h;
                                    if (!(null == g.twin || 4 >= g.twin.face.data.shape.length || null != b && -1 != b.indexOf(g.origin.point) != (-1 != b.indexOf(g.next.origin.point))) && I.distance(g.origin.point, g.next.origin.point) < k) {
                                        c = 0;
                                        for (k = this.dcel.collapseEdge(g).edges; c < k.length;) h = k[c], ++c, h.face.data.shape = h.face.getPoly();
                                        c = !0;
                                        break
                                    }
                                }
                            }
                        }
                        if (!c) break
                    }
                    null == this.dcel.vertices.h[this.center.__id__] && (this.center = Z.min(this.inner[0].shape, function(a) {
                        return a.x * a.x +
                            a.y * a.y
                    }))
                },
                buildWalls: function() {
                    C.save();
                    var a = this.waterEdge;
                    null != this.citadel && (a = a.concat(this.citadel.shape));
                    this.border = new pc(this.wallsNeeded, this, this.inner, a);
                    this.wallsNeeded && (this.wall = this.border, this.walls.push(this.wall));
                    this.gates = this.border.gates;
                    null != this.citadel && (a = new xd(this, this.citadel), a.wall.buildTowers(), this.walls.push(a.wall), this.gates = this.gates.concat(a.wall.gates));
                    C.restore()
                },
                cellsByVertex: function(a) {
                    var b = [],
                        c = 0;
                    for (a = a.edges; c < a.length;) {
                        var d = a[c];
                        ++c;
                        b.push(d.face.data)
                    }
                    return b
                },
                buildDomains: function() {
                    var a = Z.find(this.dcel.edges, function(a) {
                        return null == a.twin
                    });
                    this.horizonE = Ic.circumference(a, this.dcel.faces);
                    if (6 > this.horizonE.length) throw X.thrown("Failed to build the horizon: " + this.horizonE.length);
                    Ua.assignData(this.horizonE, Tc.HORIZON);
                    this.horizon = Ua.toPoly(this.horizonE);
                    if (this.coastNeeded) {
                        a = [];
                        for (var b = [], c = 0, d = this.dcel.faces; c < d.length;) {
                            var f = d[c];
                            ++c;
                            f.data.waterbody ? a.push(f) : b.push(f)
                        }
                        b = Z.max(Ic.split(b), function(a) {
                            return a.length
                        });
                        a = Z.max(Ic.split(a), function(a) {
                            return a.length
                        });
                        this.earthEdgeE = Ic.circumference(null, b);
                        this.earthEdge = Ua.toPoly(this.earthEdgeE);
                        this.waterEdgeE = Ic.circumference(null, a);
                        if (Z.every(this.waterEdgeE, function(a) {
                                return null != a.twin
                            })) throw X.thrown("Required water doesn't touch the horizon");
                        this.waterEdge = Ua.toPoly(this.waterEdgeE);
                        Sa.set(this.waterEdge, uc.smooth(this.waterEdge, null, Math.floor(1 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 3)));
                        for (a = 0; null != this.earthEdgeE[a].twin;) a = (a + 1) % this.earthEdgeE.length;
                        for (; null == this.earthEdgeE[a].twin;) a = (a + 1) % this.earthEdgeE.length;
                        this.shore = [];
                        this.shoreE = [];
                        do b = this.earthEdgeE[a], this.shoreE.push(b), this.shore.push(b.origin.point), a = (a + 1) % this.earthEdgeE.length; while (null != this.earthEdgeE[a].twin);
                        Ua.assignData(this.shoreE, Tc.COAST)
                    } else this.earthEdgeE = this.horizonE, this.earthEdge = this.horizon, this.waterEdgeE = [], this.waterEdge = [], this.shoreE = [], this.shore = []
                },
                buildStreets: function() {
                    for (var a = [], b = 0, c = this.cells; b < c.length;) {
                        var d = c[b];
                        ++b;
                        d.withinCity &&
                            a.push(d)
                    }
                    var f = new gh(a);
                    a = [];
                    b = 0;
                    for (c = this.cells; b < c.length;) d = c[b], ++b, d.withinCity || d.waterbody || a.push(d);
                    d = new gh(a);
                    a = [];
                    b = 0;
                    for (c = this.shoreE; b < c.length;) {
                        var h = c[b];
                        ++b;
                        a.push(h.origin)
                    }
                    f.excludePoints(a);
                    d.excludePoints(a);
                    h = [];
                    a = 0;
                    for (b = this.walls; a < b.length;) {
                        var k = b[a];
                        ++a;
                        Z.addAll(h, Ua.vertices(k.edges))
                    }
                    0 < h.length && (Z.removeAll(h, this.gates), f.excludePoints(h), d.excludePoints(h));
                    h = Z.difference(this.earthEdgeE, this.shoreE);
                    a = [];
                    b = 0;
                    for (c = h; b < c.length;) {
                        var n = c[b];
                        ++b;
                        null != d.pt2node.h.__keys__[n.origin.__id__] &&
                            a.push(n)
                    }
                    h = a;
                    a = 0;
                    for (b = this.gates; a < b.length;)
                        if (k = [b[a]], ++a, c = null != this.plaza ? Z.min(this.plaza.shape, function(a) {
                                return function(b) {
                                    var c = a[0].point,
                                        d = b.x - c.x;
                                    b = b.y - c.y;
                                    return d * d + b * b
                                }
                            }(k)) : this.center, c = f.buildPath(k[0], this.dcel.vertices.h[c.__id__]), null != c) {
                            if (c = this.dcel.vertices2chain(c), this.streets.push(c), -1 != this.border.gates.indexOf(k[0])) {
                                n = null;
                                if (null != d.pt2node.h.__keys__[k[0].__id__])
                                    for (h = Z.sortBy(h, function(a) {
                                            return function(b) {
                                                b = b.origin.point;
                                                var c = a[0].point;
                                                return -(c.x *
                                                    b.x + c.y * b.y) / b.get_length()
                                            }
                                        }(k)), c = 0; c < h.length && (n = h[c], ++c, n = d.buildPath(n.origin, k[0]), null == n););
                                c = n;
                                if (null != c) k = this.dcel.vertices2chain(c), d.excludePolygon(k), this.roads.push(k);
                                else if (null != this.wall) {
                                    c = [];
                                    n = 0;
                                    for (k = this.cellsByVertex(k[0]); n < k.length;) {
                                        var p = k[n];
                                        ++n;
                                        !p.withinWalls && p.bordersInside(this.shoreE) && c.push(p)
                                    }
                                    k = c;
                                    for (c = 0; c < k.length;) n = k[c], ++c, n.landing = !0, n.withinCity = !0, new Pc(this, n), this.maxDocks--
                                }
                            }
                        } else hb.trace("Unable to build a street!", {
                            fileName: "Source/com/watabou/mfcg/model/City.hx",
                            lineNumber: 591,
                            className: "com.watabou.mfcg.model.City",
                            methodName: "buildStreets"
                        });
                    this.tidyUpRoads();
                    if (this.wallsNeeded) {
                        a = [];
                        b = 0;
                        for (c = this.gates; b < c.length;) f = c[b], ++b, a.push(f.point);
                        f = a
                    } else f = null;
                    a = 0;
                    for (b = this.arteries; a < b.length;) d = b[a], ++a, Ua.assignData(d, Tc.ROAD), d = Ua.toPoly(d), Sa.set(d, uc.smoothOpen(d, f, 2))
                },
                tidyUpRoads: function() {
                    for (var a = [], b = 0, c = this.streets; b < c.length;) {
                        var d = c[b];
                        ++b;
                        Z.addAll(a, d)
                    }
                    b = 0;
                    for (c = this.roads; b < c.length;) d = c[b], ++b, Z.addAll(a, d);
                    for (this.arteries = []; 0 <
                        a.length;) {
                        d = a.pop();
                        var f = !1;
                        b = 0;
                        for (c = this.arteries; b < c.length;) {
                            var h = c[b];
                            ++b;
                            if (h[0].origin == d.next.origin) {
                                h.unshift(d);
                                f = !0;
                                break
                            } else if (h[h.length - 1].next.origin == d.origin) {
                                h.push(d);
                                f = !0;
                                break
                            }
                        }
                        f || this.arteries.push([d])
                    }
                },
                buildCanals: function() {
                    C.save();
                    this.canals = this.riverNeeded ? [yb.createRiver(this)] : [];
                    C.restore()
                },
                addHarbour: function(a) {
                    for (var b = 0, c = [], d = a = a.face.halfEdge, f = !0; f;) {
                        var h = d;
                        d = d.next;
                        f = d != a;
                        null != h.twin && c.push(h.twin.face.data)
                    }
                    for (; b < c.length;) a = c[b], ++b, a.waterbody &&
                        null == a.ward && new lf(this, a)
                },
                createWards: function() {
                    if (this.bp.greens) {
                        var a = 0;
                        if (null != this.citadel) {
                            var b = this.cellsByVertex(this.citadel.ward.wall.gates[0]);
                            if (3 == b.length) {
                                var c = 1 - 2 / (this.nPatches - 1);
                                null == c && (c = .5);
                                var d = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c
                            } else d = !1;
                            if (d)
                                for (d = 0; d < b.length;) {
                                    var f = b[d];
                                    ++d;
                                    null == f.ward && (new ce(this, f), ++a)
                                }
                        }
                        d = (this.nPatches - 10) / 20;
                        c = d - (d | 0);
                        null == c && (c = .5);
                        a = (d | 0) + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c ? 1 : 0) - a;
                        for (d = 0; d < a;)
                            for (d++;;)
                                if (f =
                                    Z.random(this.inner), null == f.ward) {
                                    new ce(this, f);
                                    break
                                }
                    }
                    if (0 < this.shoreE.length && 0 < this.maxDocks)
                        for (d = 0, a = this.inner; d < a.length && !(f = a[d], ++d, f.bordersInside(this.shoreE) && (f.landing = !0, 0 >= --this.maxDocks)););
                    this.templeNeeded && (f = Z.min(this.inner, function(a) {
                        return null == a.ward ? Sa.center(a.shape).get_length() : Infinity
                    }), new Ne(this, f));
                    d = 0;
                    for (a = this.inner; d < a.length;) f = a[d], ++d, null == f.ward && new Pc(this, f);
                    if (null != this.wall)
                        for (d = 0, a = this.wall.gates; d < a.length;)
                            if (b = a[d], ++d, c = 1 / (this.nPatches -
                                    5), null == c && (c = .5), !((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c))
                                for (c = 0, b = this.cellsByVertex(b); c < b.length;) f = b[c], ++c, null == f.ward && (f.withinCity = !0, f.bordersInside(this.shoreE) && 0 < this.maxDocks-- && (f.landing = !0), new Pc(this, f));
                    this.shantyNeeded && this.buildShantyTowns();
                    d = 0;
                    for (a = Ua.vertices(this.shoreE); d < a.length;)
                        for (f = a[d], ++d, c = 0, b = this.cellsByVertex(f); c < b.length;) {
                            var h = b[c];
                            ++c;
                            if (h.withinCity && !h.landing) {
                                for (var k = h.face.halfEdge; k.next.origin != f;) k = k.next;
                                if (k.twin.face.data.landing &&
                                    k.next.twin.face.data.landing) {
                                    h.landing = !0;
                                    break
                                }
                            }
                        }
                    d = 0;
                    for (a = this.cells; d < a.length;) f = a[d], ++d, f.landing && this.addHarbour(f);
                    this.buildFarms()
                },
                updateDimensions: function() {
                    for (var a = 0, b = 0, c = 0, d = 0, f = function(f) {
                            for (var h = 0; h < f.length;) {
                                var k = f[h];
                                ++h;
                                k.x < a ? a = k.x : k.x > b && (b = k.x);
                                k.y < c ? c = k.y : k.y > d && (d = k.y)
                            }
                        }, h = 0, k = this.districts; h < k.length;) {
                        var n = k[h];
                        ++h;
                        var p = 0;
                        for (n = n.groups; p < n.length;) {
                            var g = n[p];
                            ++p;
                            var q = 0;
                            for (g = g.blocks; q < g.length;) {
                                var m = g[q];
                                ++q;
                                f(m.shape)
                            }
                        }
                    }
                    null != this.citadel && f(va.__cast(this.citadel.ward,
                        xd).wall.shape);
                    this.bounds.setTo(a, c, b - a, d - c)
                },
                getViewport: function() {
                    return null != this.focus ? this.focus.getBounds() : this.bounds
                },
                buildFarms: function() {
                    for (var a = ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2, b = ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3, c = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * Math.PI *
                            2, d = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * Math.PI * 2, f = 0, h = 0, k = this.inner; h < k.length;) {
                        var n = k[h];
                        ++h;
                        var p = 0;
                        for (n = n.shape; p < n.length;) {
                            var g = n[p];
                            ++p;
                            f = Math.max(f, I.distance(g, this.center))
                        }
                    }
                    h = 0;
                    for (k = this.cells; h < k.length;) n = k[h], ++h, null == n.ward && (n.waterbody ? new Rb(this, n) : n.bordersInside(this.shoreE) ? new og(this, n) : (p = Sa.center(n.shape).subtract(this.center), g = Math.atan2(p.y, p.x), g = a * Math.sin(g + c) + b * Math.sin(2 * g + d), p.get_length() < (g + 1) * f ? new yd(this, n) : new og(this, n)))
                },
                buildShantyTowns: function() {
                    for (var a =
                            this, b = [], c = [], d = function(b) {
                                for (var c = 3 * I.distance(b, a.center), d = 0, f = a.roads; d < f.length;) {
                                    var h = f[d];
                                    ++d;
                                    for (var k = 0; k < h.length;) {
                                        var n = h[k];
                                        ++k;
                                        c = Math.min(c, 2 * I.distance(n.origin.point, b))
                                    }
                                }
                                d = 0;
                                for (f = a.shoreE; d < f.length;) k = f[d], ++d, c = Math.min(c, I.distance(k.origin.point, b));
                                d = 0;
                                for (f = a.canals; d < f.length;)
                                    for (n = f[d], ++d, k = 0, h = n.course; k < h.length;) n = h[k], ++k, c = Math.min(c, I.distance(n.origin.point, b));
                                return c * c
                            }, f = function(f) {
                                for (var h = 0, k = [], n = f.face.halfEdge, p = n, g = !0; g;) {
                                    var q = p;
                                    p = p.next;
                                    g = p != n;
                                    null != q.twin && k.push(q.twin.face.data)
                                }
                                for (f = k; h < f.length;) {
                                    var P = f[h];
                                    ++h;
                                    if (!P.withinCity && !P.waterbody && !P.bordersInside(a.horizonE)) {
                                        k = [];
                                        p = n = P.face.halfEdge;
                                        for (g = !0; g;) q = p, p = p.next, g = p != n, null != q.twin && k.push(q.twin.face.data);
                                        k = Z.count(k, function(a) {
                                            return a.withinCity
                                        });
                                        1 < k && Z.add(b, P) && c.push(k * k / d(Sa.center(P.shape)))
                                    }
                                }
                            }, h = 0, k = this.cells; h < k.length;) {
                        var n = k[h];
                        ++h;
                        n.withinCity && f(n)
                    }
                    var p = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647;
                    for (p = this.nPatches * (1 + p * p * p) * .5; 0 < p && 0 < b.length;) {
                        h = [];
                        k = 0;
                        for (n = c.length; k < n;) {
                            var g = k++;
                            h.push(g)
                        }
                        h = Z.weighted(h, c);
                        n = b[h];
                        n.withinCity = !0;
                        0 < this.maxDocks && n.bordersInside(this.shoreE) && (n.landing = !0, this.maxDocks--);
                        new Pc(this, n);
                        c.splice(h, 1);
                        N.remove(b, n);
                        --p;
                        f(n)
                    }
                },
                buildCityTowers: function() {
                    if (null != this.wall) {
                        for (var a = 0, b = this.wall.edges.length; a < b;) {
                            var c = a++,
                                d = this.wall.edges[c];
                            if (d.data == Tc.COAST || d.twin.face.data == this.citadel) this.wall.segments[c] = !1
                        }
                        this.wall.buildTowers();
                        if (null != this.citadel)
                            for (a = 0, b = va.__cast(this.citadel.ward,
                                    xd).wall.towers; a < b.length;) c = b[a], ++a, N.remove(this.wall.towers, c)
                    }
                },
                getOcean: function() {
                    if (null != this.ocean) return this.ocean;
                    for (var a = [], b = !1, c = 0, d = this.waterEdgeE; c < d.length;) {
                        var f = d[c];
                        ++c;
                        var h = f.origin.point;
                        if (null == f.twin) a.push(h);
                        else {
                            f = f.twin.face.data;
                            var k = !1,
                                n = !1;
                            f.landing ? k = !0 : f.withinCity && kf.isConvexVertexi(this.earthEdge, this.earthEdge.indexOf(h)) && (n = !0);
                            if (b || k) n = !0;
                            b = k;
                            n && a.push(h)
                        }
                    }
                    return this.ocean = Hf.render(this.waterEdge, !0, 3, a)
                },
                buildGeometry: function() {
                    for (var a = 0, b =
                            this.canals; a < b.length;) {
                        var c = b[a];
                        ++a;
                        c.updateState()
                    }
                    Ma.reset();
                    this.name = null != this.bp.name ? this.bp.name : this.rerollName();
                    a = new ik(this);
                    a.build();
                    this.districts = a.districts;
                    Ma.nameDistricts(this);
                    a = 0;
                    for (b = this.cells; a < b.length;) c = b[a], ++a, c.ward.createGeometry()
                },
                rerollDistricts: function() {
                    Ma.reset();
                    Ma.nameDistricts(this);
                    Bb.districtsChanged.dispatch()
                },
                updateGeometry: function(a) {
                    for (var b = [], c = [], d = 0; d < a.length;) {
                        var f = a[d];
                        ++d;
                        f.ward instanceof Pc ? Z.add(c, f.ward.group.core) : f.ward.createGeometry();
                        null != f.district && Z.add(b, f.district)
                    }
                    for (d = 0; d < c.length;) f = c[d], ++d, f.data.ward.createGeometry();
                    for (d = 0; d < b.length;) c = b[d], ++d, c.updateGeometry();
                    gc.resetForests();
                    Bb.geometryChanged.dispatch(1 == a.length ? a[0] : null)
                },
                updateLots: function() {
                    for (var a = 0, b = this.cells; a < b.length;) {
                        var c = b[a];
                        ++a;
                        c.ward instanceof Pc && c.ward.createGeometry()
                    }
                    Bb.geometryChanged.dispatch(null)
                },
                addLandmark: function(a) {
                    a = new di(this, a);
                    this.landmarks.push(a);
                    Bb.landmarksChanged.dispatch();
                    return a
                },
                updateLandmarks: function() {
                    for (var a =
                            0, b = this.landmarks; a < b.length;) {
                        var c = b[a];
                        ++a;
                        c.update()
                    }
                },
                addLandmarks: function(a) {
                    for (var b = [], c = 0, d = this.districts; c < d.length;) {
                        var f = d[c];
                        ++c;
                        var h = 0;
                        for (f = f.groups; h < f.length;) {
                            var k = f[h];
                            ++h;
                            var n = 0;
                            for (k = k.blocks; n < k.length;) {
                                var p = k[n];
                                ++n;
                                p = p.lots;
                                for (var g = 0; g < p.length;) {
                                    var q = p[g];
                                    ++g;
                                    b.push(q)
                                }
                            }
                        }
                    }
                    for (c = 0; c < a.length;) h = a[c], ++c, d = Z.random(b), f = Sa.center(d), h = new di(this, f, h), this.landmarks.push(h), N.remove(b, d);
                    Bb.landmarksChanged.dispatch()
                },
                removeLandmark: function(a) {
                    N.remove(this.landmarks,
                        a);
                    Bb.landmarksChanged.dispatch()
                },
                removeLandmarks: function() {
                    this.landmarks = [];
                    Bb.landmarksChanged.dispatch()
                },
                countBuildings: function() {
                    for (var a = 0, b = 0, c = this.districts; b < c.length;) {
                        var d = c[b];
                        ++b;
                        var f = 0;
                        for (d = d.groups; f < d.length;) {
                            var h = d[f];
                            ++f;
                            var k = 0;
                            for (h = h.blocks; k < h.length;) {
                                var n = h[k];
                                ++k;
                                a += n.lots.length
                            }
                        }
                    }
                    return a
                },
                getNeighbour: function(a, b) {
                    for (var c = a = a.face.halfEdge, d = !0; d;) {
                        var f = c;
                        c = c.next;
                        d = c != a;
                        if (f.origin == b) {
                            b = f.twin;
                            b = null != b ? b.face : null;
                            if (null != b) return b.data;
                            break
                        }
                    }
                    return null
                },
                getCell: function(a) {
                    for (var b = 0, c = this.cells; b < c.length;) {
                        var d = c[b];
                        ++b;
                        if (Gb.containsPoint(d.shape, a)) return d
                    }
                    return null
                },
                getDetails: function(a) {
                    for (var b = 0, c = 0, d = this.districts; c < d.length;) {
                        var f = d[c];
                        ++c;
                        var h = 0;
                        for (f = f.groups; h < f.length;) {
                            var k = f[h];
                            ++h;
                            if (null == this.focus || Z.intersects(k.faces, this.focus.faces)) {
                                var n = 0;
                                for (k = k.blocks; n < k.length;) {
                                    var p = k[n];
                                    ++n;
                                    Gb.rect(p.shape).intersects(a) && ++b
                                }
                            }
                        }
                    }
                    c = 0;
                    for (d = this.walls; c < d.length;)
                        for (f = d[c], ++c, h = 0, f = f.towers; h < f.length;) n = f[h], ++h, (null ==
                            this.focus || -1 != this.focus.vertices.indexOf(n)) && a.containsPoint(n.point) && ++b;
                    return b
                },
                splitEdge: function(a) {
                    var b = this.dcel.splitEdge(a);
                    a.face.data.shape = a.face.getPoly();
                    a.twin.face.data.shape = a.twin.face.getPoly();
                    return b
                },
                getCanalWidth: function(a) {
                    return this.canals[0].width
                },
                getFMGParams: function() {
                    return {
                        size: this.nPatches,
                        seed: this.bp.seed,
                        name: this.name,
                        coast: 0 < this.shoreE.length ? 1 : 0,
                        port: Z.some(this.cells, function(a) {
                            return a.landing
                        }) ? 1 : 0,
                        river: 0 < this.canals.length ? 1 : 0,
                        sea: 0 < this.shoreE.length ?
                            this.bp.coastDir : 0
                    }
                },
                __class__: Ub
            };
            var pc = function(a, b, c, d) {
                this.watergates = new pa;
                this.real = !0;
                this.patches = c;
                if (1 == c.length) {
                    for (var f = [], h = c[0].face.halfEdge, k = h, n = !0; n;) {
                        var p = k;
                        k = k.next;
                        n = k != h;
                        f.push(p)
                    }
                    this.edges = f;
                    this.shape = c[0].shape
                } else {
                    f = [];
                    for (h = 0; h < c.length;) k = c[h], ++h, f.push(k.face);
                    this.edges = Ic.circumference(null, f);
                    this.shape = Ua.toPoly(this.edges)
                }
                a && (Ua.assignData(this.edges, Tc.WALL, !1), 1 < c.length && Sa.set(this.shape, uc.smooth(this.shape, d, 3)));
                this.length = this.shape.length;
                1 == c.length ?
                    this.buildCastleGate(b, d) : this.buildCityGates(a, b, d);
                f = [];
                h = 0;
                for (a = this.shape; h < a.length;) ++h, f.push(!0);
                this.segments = f
            };
