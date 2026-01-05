/*
 * model-core/04-curtain-wall.js
 * Part 4/8: Curtain Wall
 * Contains: CurtainWall class (city walls)
 */
            g["com.watabou.mfcg.model.CurtainWall"] = pc;
            pc.__name__ = "com.watabou.mfcg.model.CurtainWall";
            pc.prototype = {
                buildCityGates: function(a, b, c) {
                    this.gates = [];
                    for (var d = [], f = 0, h = this.edges; f < h.length;) {
                        var k = h[f];
                        ++f;
                        d.push(-1 != c.indexOf(k.origin.point) || 2 > Z.intersect(b.cellsByVertex(k.origin), this.patches).length ? 0 : 1)
                    }
                    var n = d;
                    if (0 == Z.sum(n)) throw hb.trace("" + this.length + " vertices of " +
                        this.patches.length + " patches, " + c.length + " are reserved.", {
                            fileName: "Source/com/watabou/mfcg/model/CurtainWall.hx",
                            lineNumber: 82,
                            className: "com.watabou.mfcg.model.CurtainWall",
                            methodName: "buildCityGates"
                        }), new Vb("No valid vertices to create gates!");
                    for (var p = -1 < b.bp.gates ? b.bp.gates : b.bp.hub ? this.shape.length : 2 + (this.patches.length / 12 * (0 < b.shoreE.length ? .75 : 1) | 0); this.gates.length < p && 0 < Z.sum(n);) {
                        d = [];
                        f = 0;
                        for (h = n.length; f < h;) k = f++, d.push(k);
                        d = Z.weighted(d, n);
                        f = [this.edges[d].origin];
                        this.gates.push(f[0]);
                        if (a && (k = Z.difference(b.cellsByVertex(f[0]), this.patches), 1 == k.length)) {
                            k = k[0];
                            h = Z.difference(k.shape, c);
                            var g = this.shape,
                                q = g.indexOf(f[0].point);
                            if (-1 != q) {
                                var m = g.length;
                                g = g[(q + m - 1) % m]
                            } else g = null;
                            q = this.shape;
                            m = q.indexOf(f[0].point);
                            q = -1 != m ? q[(m + 1) % q.length] : null;
                            Z.removeAll(h, this.shape);
                            if (0 < h.length) {
                                g = [f[0].point.subtract(qa.lerp(g, q))];
                                h = Z.max(h, function(a, b) {
                                    return function(c) {
                                        c = c.subtract(b[0].point);
                                        return (c.x * a[0].x + c.y * a[0].y) / c.get_length()
                                    }
                                }(g, f));
                                f = b.dcel.splitFace(k.face, f[0], b.dcel.vertices.h[h.__id__]);
                                f = [f.face, f.twin.face];
                                h = b.cells;
                                g = [];
                                for (q = 0; q < f.length;) m = f[q], ++q, g.push(new ci(m));
                                Z.replace(h, k, g);
                                for (h = 0; h < f.length;)
                                    for (k = f[h], ++h, q = g = k.halfEdge, m = !0; m;) k = q, q = q.next, m = q != g, null != k.twin && k.twin.data == Tc.WALL && (k.data = Tc.WALL)
                            }
                        }
                        k = 0;
                        for (f = n.length; k < f;) h = k++, g = Math.abs(h - d), g > n.length / 2 && (g = n.length - g), n[h] *= 1 >= g ? 0 : g - 1
                    }
                    if (0 == this.gates.length && 0 < p) throw new Vb("No gates created!");
                    if (a)
                        for (d = 0, f = this.gates; d < f.length;) a = f[d], ++d, wd.set(a.point, uc.lerpVertex(this.shape, a.point))
                },
                buildCastleGate: function(a,
                    b) {
                    for (var c = 0, d = this.edges; c < d.length;) {
                        var f = d[c];
                        ++c;
                        if (f.twin.face.data == a.plaza) {
                            this.gates = [this.splitSegment(a, f)];
                            return
                        }
                    }
                    c = Z.difference(this.shape, b);
                    if (0 == c.length) {
                        c = [];
                        d = 0;
                        for (b = this.edges; d < b.length;) f = b[d], ++d, f.twin.face.data.withinCity && c.push(f);
                        0 == c.length ? (hb.trace("No suitable edge to split", {
                            fileName: "Source/com/watabou/mfcg/model/CurtainWall.hx",
                            lineNumber: 169,
                            className: "com.watabou.mfcg.model.CurtainWall",
                            methodName: "buildCastleGate"
                        }), this.gates = [Z.min(this.edges, function(a) {
                            return I.distance(a.origin.point,
                                a.next.origin.point)
                        }).origin]) : (f = Z.min(c, function(a) {
                            return qa.lerp(a.origin.point, a.next.origin.point, .5).get_length()
                        }), this.gates = [this.splitSegment(a, f)])
                    } else c = Z.min(c, function(a) {
                        return a.get_length()
                    }), wd.set(c, uc.lerpVertex(this.shape, c)), this.gates = [a.dcel.vertices.h[c.__id__]]
                },
                splitSegment: function(a, b) {
                    a = a.splitEdge(b);
                    for (var c = [], d = this.patches[0].face.halfEdge, f = d, h = !0; h;) b = f, f = f.next, h = f != d, c.push(b);
                    this.edges = c;
                    this.shape = this.patches[0].shape;
                    this.length++;
                    Ua.assignData(this.edges,
                        Tc.WALL, !1);
                    return a.origin
                },
                buildTowers: function() {
                    this.towers = [];
                    this.coastTowers = [];
                    if (this.real)
                        for (var a = 0, b = this.length; a < b;) {
                            var c = a++,
                                d = (c + this.length - 1) % this.length,
                                f = this.edges[c].origin;
                            if (-1 == this.gates.indexOf(f) && (this.segments[d] || this.segments[c])) {
                                this.towers.push(f);
                                for (var h = null, k = null, n = 0, p = f.edges; n < p.length;) {
                                    var g = p[n];
                                    ++n;
                                    g.data == Tc.COAST && (null == h ? h = g : k = g)
                                }
                                null != k && (h = h.face.data.waterbody ? [k.next.origin, h.next.origin] : [h.next.origin, k.next.origin], h.push(f), h.push(this.edges[this.segments[c] ?
                                    (c + 1) % this.length : d].origin), this.coastTowers.push(h))
                            }
                        }
                },
                bothSegments: function(a) {
                    return this.segments[a] ? this.segments[(a + this.length - 1) % this.length] : !1
                },
                addWatergate: function(a, b) {
                    this.watergates.set(a, b);
                    N.remove(this.towers, a)
                },
                getTowerRadius: function(a) {
                    return this.real ? -1 != this.towers.indexOf(a) ? pc.LTOWER_RADIUS : -1 != this.gates.indexOf(a) ? 1 + 2 * pc.TOWER_RADIUS : 0 : 0
                },
                __class__: pc
            };
            var lc = y["com.watabou.mfcg.model.DistrictType"] = {
                __ename__: "com.watabou.mfcg.model.DistrictType",
                __constructs__: null,
                CENTER: (G = function(a) {
                    return {
                        _hx_index: 0,
                        plaza: a,
                        __enum__: "com.watabou.mfcg.model.DistrictType",
                        toString: r
                    }
                }, G._hx_name = "CENTER", G.__params__ = ["plaza"], G),
                CASTLE: (G = function(a) {
                    return {
                        _hx_index: 1,
                        castle: a,
                        __enum__: "com.watabou.mfcg.model.DistrictType",
                        toString: r
                    }
                }, G._hx_name = "CASTLE", G.__params__ = ["castle"], G),
                DOCKS: {
                    _hx_name: "DOCKS",
                    _hx_index: 2,
                    __enum__: "com.watabou.mfcg.model.DistrictType",
                    toString: r
                },
                BRIDGE: (G = function(a) {
                    return {
                        _hx_index: 3,
                        bridge: a,
                        __enum__: "com.watabou.mfcg.model.DistrictType",
                        toString: r
                    }
                }, G._hx_name = "BRIDGE", G.__params__ = ["bridge"], G),
                GATE: (G = function(a) {
                    return {
                        _hx_index: 4,
                        gate: a,
                        __enum__: "com.watabou.mfcg.model.DistrictType",
                        toString: r
                    }
                }, G._hx_name = "GATE", G.__params__ = ["gate"], G),
                BANK: {
                    _hx_name: "BANK",
                    _hx_index: 5,
                    __enum__: "com.watabou.mfcg.model.DistrictType",
                    toString: r
                },
                PARK: {
                    _hx_name: "PARK",
                    _hx_index: 6,
                    __enum__: "com.watabou.mfcg.model.DistrictType",
                    toString: r
                },
                SPRAWL: {
                    _hx_name: "SPRAWL",
                    _hx_index: 7,
                    __enum__: "com.watabou.mfcg.model.DistrictType",
                    toString: r
                },
                REGULAR: {
                    _hx_name: "REGULAR",
                    _hx_index: 8,
                    __enum__: "com.watabou.mfcg.model.DistrictType",
                    toString: r
                }
            };
            lc.__constructs__ = [lc.CENTER, lc.CASTLE, lc.DOCKS, lc.BRIDGE, lc.GATE, lc.BANK, lc.PARK, lc.SPRAWL, lc.REGULAR];
            var Pe = function(a, b) {
                this.type = b;
                this.city = a[0].ward.model;
                b = [];
                for (var c = 0; c < a.length;) {
                    var d = a[c];
                    ++c;
                    b.push(d.face)
                }
                this.faces = b;
                for (b = 0; b < a.length;) c = a[b], ++b, c.district = this;
                this.createParams()
            };
