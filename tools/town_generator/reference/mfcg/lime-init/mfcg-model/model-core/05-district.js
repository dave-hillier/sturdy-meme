/*
 * model-core/05-district.js
 * Part 5/8: District
 * Contains: District, DistrictBuilder, Grower classes
 */
            g["com.watabou.mfcg.model.District"] = Pe;
            Pe.__name__ = "com.watabou.mfcg.model.District";
            Pe.check4holes = function(a) {
                for (var b = [],
                        c = [], d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    for (var h = f.halfEdge, k = h, n = !0; n;)
                        if (f = k, k = k.next, n = k != h, null == f.twin || -1 == a.indexOf(f.twin.face)) {
                            if (-1 != c.indexOf(f.origin)) return !0;
                            b.push(f);
                            c.push(f.origin)
                        }
                }
                a = 0;
                f = c = b[0];
                do
                    for (++a, f = f.next; - 1 == b.indexOf(f);) f = f.twin.next; while (f != c);
                return b.length > a
            };
            Pe.updateColors = function(a) {
                var b = a.length;
                if (1 == b) a[0].color = K.colorRoof;
                else
                    for (var c = 0; c < b;) {
                        var d = c++;
                        a[d].color = K.getTint(K.colorRoof, d, b)
                    }
            };
            Pe.prototype = {
                createParams: function() {
                    this.alleys = {
                        minSq: 15 +
                            40 * Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1),
                        gridChaos: .2 + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * .8,
                        sizeChaos: .4 + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) /
                            2147483647) / 3 * .6,
                        shapeFactor: .25 + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2,
                        inset: .6 * (1 - Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1)),
                        blockSize: 4 + 10 * (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed =
                            48271 * C.seed % 2147483647 | 0) / 2147483647) / 3)
                    };
                    this.alleys.minFront = Math.sqrt(this.alleys.minSq);
                    this.greenery = Math.pow(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3, this.type == lc.PARK ? 1 : 2);
                    this.type == lc.SPRAWL && (this.alleys.gridChaos *= .5, this.alleys.blockSize *= 2, this.greenery = (1 + this.greenery) / 2)
                },
                updateGeometry: function() {
                    if (1 == this.faces.length) {
                        var a = [];
                        for (var b = this.faces[0].halfEdge, c = b, d = !0; d;) {
                            var f =
                                c;
                            c = c.next;
                            d = c != b;
                            a.push(f)
                        }
                    } else a = Ic.circumference(null, this.faces);
                    this.border = a;
                    this.equator = this.ridge = null
                },
                createGroups: function() {
                    for (var a = [], b = 0, c = this.faces; b < c.length;) {
                        var d = c[b];
                        ++b;
                        d.data.ward instanceof Pc && a.push(d)
                    }
                    b = a;
                    for (a = []; 0 < b.length;) {
                        var f = this.pickFaces(b);
                        a.push(new Qe(f))
                    }
                    this.groups = a;
                    a = 0;
                    for (b = this.groups; a < b.length;)
                        if (f = b[a], ++a, 1 < f.faces.length) {
                            c = [];
                            for (var h = 0, k = f.border; h < k.length;) d = k[h], ++h, d = d.origin, Z.some(d.edges, function(a) {
                                return null != a.data ? a.data != Tc.HORIZON :
                                    !1
                            }) ? c.push(d.point) : Z.some(this.city.cellsByVertex(d), function(a) {
                                return !(a.ward instanceof Pc)
                            }) && c.push(d.point);
                            f = Ua.toPoly(f.border);
                            c = uc.smooth(f, c, 2);
                            Sa.set(f, c)
                        }
                },
                pickFaces: function(a) {
                    for (var b = [Z.pick(a)];;) {
                        var c = b.length;
                        1 < a.length ? (c = (c - 3) / c, null == c && (c = .5), c = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < c) : c = !1;
                        if (c) break;
                        c = [];
                        for (var d = 0; d < b.length;) {
                            var f = b[d];
                            ++d;
                            for (var h = f = f.halfEdge, k = !0; k;) {
                                var n = h;
                                h = h.next;
                                k = h != f;
                                null == n.data && (n = n.twin.face, -1 != a.indexOf(n) && -1 == b.indexOf(n) &&
                                    c.push(n))
                            }
                        }
                        if (Z.isEmpty(c)) break;
                        else c = Z.pick(c), N.remove(a, c), b.push(c)
                    }
                    1 < b.length && Pe.check4holes(b) && (hb.trace("Hole in a group, we need to split it", {
                        fileName: "Source/com/watabou/mfcg/model/District.hx",
                        lineNumber: 175,
                        className: "com.watabou.mfcg.model.District",
                        methodName: "pickFaces"
                    }), Z.addAll(a, b.slice(1)), b = [b[0]]);
                    return b
                },
                getRidge: function() {
                    var a = Ua.toPoly(this.border).slice();
                    uc.simplify(a);
                    if ("Curved" == ba.get("districts", "Curved")) return null == this.ridge && (this.ridge = Xk.build(a)), this.ridge;
                    null == this.equator && (this.equator = ze.build(a));
                    return this.equator
                },
                __class__: Pe
            };
            var ik = function(a) {
                this.model = a
            };
            g["com.watabou.mfcg.model.DistrictBuilder"] = ik;
            ik.__name__ = "com.watabou.mfcg.model.DistrictBuilder";
            ik.prototype = {
                build: function() {
                    for (var a = this, b = [], c = 0, d = this.model.cells; c < d.length;) {
                        var f = d[c];
                        ++c;
                        f.withinCity && b.push(f)
                    }
                    this.city = b;
                    this.unassigned = this.city.slice();
                    this.districts = [];
                    var h = [],
                        k = function(a, b) {
                            h.push({
                                vertex: a,
                                type: b
                            })
                        };
                    d = function(b, c) {
                        h.push({
                            patch: b,
                            type: null != c ?
                                c : a.getType(b)
                        })
                    };
                    null != this.model.citadel && k(va.__cast(this.model.citadel.ward, xd).wall.gates[0], lc.CASTLE(this.model.citadel));
                    null != this.model.plaza ? d(this.model.plaza, lc.CENTER(this.model.plaza)) : k(this.model.dcel.vertices.h[this.model.center.__id__], lc.CENTER(null));
                    b = 0;
                    for (c = this.unassigned; b < c.length;) f = c[b], ++b, f.ward instanceof ce && d(f, lc.PARK);
                    if (null != this.model.wall)
                        for (b = 0, c = this.model.wall.gates; b < c.length;) {
                            var n = c[b];
                            ++b;
                            k(n, lc.GATE(n))
                        }
                    b = 0;
                    for (c = this.model.canals; b < c.length;) {
                        n = c[b];
                        ++b;
                        var p = n.bridges;
                        f = p;
                        for (p = p.keys(); p.hasNext();) {
                            var g = p.next();
                            f.get(g);
                            k(g, lc.BRIDGE(g))
                        }
                        f = Z.random(n.course);
                        f.face.data.withinCity && d(f.face.data, lc.BANK);
                        n = Z.random(n.course).twin;
                        n.face.data.withinCity && d(n.face.data, lc.BANK)
                    }
                    b = 0;
                    for (c = this.unassigned; b < c.length;)
                        if (f = c[b], ++b, f.landing && f.ward instanceof Pc) {
                            d(f, lc.DOCKS);
                            break
                        } b = 0;
                    for (c = h.length; b < c;) b++, d(Z.random(this.city));
                    b = Math.sqrt(this.city.length) | 0;
                    for (h = Z.subset(h, b); h.length < b;) d(Z.random(this.city));
                    b = [];
                    c = 0;
                    for (d = h; c < d.length;) k =
                        d[c], ++c, k.type == lc.DOCKS && b.push(k);
                    1 < b.length && (Z.removeAll(h, b), h.push(Z.random(b)));
                    for (b = 0; b < h.length;) c = h[b], ++b, null != c.patch ? this.fromPatch(c.patch, c.type) : this.fromVertex(c.vertex, c.type);
                    this.growAll();
                    b = 0;
                    for (c = this.districts; b < c.length;) d = c[b], ++b, d.updateGeometry(), d.createGroups();
                    this.sort(this.districts, this.model.cells[0]);
                    Pe.updateColors(this.districts)
                },
                fromPatch: function(a, b) {
                    return null == a.district ? (b = new Pe([a], b), this.districts.push(b), N.remove(this.unassigned, a), b) : null
                },
                fromVertex: function(a,
                    b, c) {
                    null == c && (c = !0);
                    var d = this.model.cellsByVertex(a);
                    d = Z.intersect(d, this.city);
                    a = [];
                    for (var f = 0; f < d.length;) {
                        var h = d[f];
                        ++f;
                        null == h.district && a.push(h)
                    }
                    if (0 == a.length || c && a.length < d.length) return null;
                    b = new Pe(a, b);
                    this.districts.push(b);
                    Z.removeAll(this.unassigned, a);
                    return b
                },
                getType: function(a) {
                    return a.ward instanceof xd ? lc.CASTLE(a) : a.ward instanceof ce ? lc.PARK : a.landing && a.ward instanceof Pc ? lc.DOCKS : -1 != this.model.inner.indexOf(a) ? lc.REGULAR : lc.SPRAWL
                },
                growAll: function() {
                    for (var a = [], b =
                            0, c = this.districts; b < c.length;) {
                        var d = c[b];
                        ++b;
                        a.push(this.getGrower(d))
                    }
                    for (b = a; 0 < b.length;)
                        for (c = Z.shuffle(b), a = 0; a < c.length;)
                            if (d = c[a], ++a, d.grow(this.unassigned) || N.remove(b, d), 0 == this.unassigned.length) return;
                    for (; 0 < this.unassigned.length;)
                        for (a = Z.random(this.unassigned), a = this.fromPatch(a, this.getType(a)), d = this.getGrower(a); d.grow(this.unassigned););
                },
                getGrower: function(a) {
                    switch (a.type._hx_index) {
                        case 2:
                            return new ei(a);
                        case 6:
                            return new fi(a);
                        default:
                            return new Re(a)
                    }
                },
                sort: function(a, b) {
                    for (var c =
                            new pa, d = 0; d < a.length;) {
                        var f = a[d];
                        ++d;
                        null == f.equator && (f.equator = ze.build(Ua.toPoly(f.border)));
                        c.set(f, qa.lerp(f.equator[0], f.equator[1]))
                    }
                    var h = c,
                        k = Sa.centroid(b.shape);
                    a.sort(function(a, b) {
                        a = I.distance(k, h.h[a.__id__]);
                        b = I.distance(k, h.h[b.__id__]);
                        b = a - b;
                        return 0 == b ? 0 : 0 > b ? -1 : 1
                    });
                    for (c = 0; c < a.length;)
                        if (f = a[c], ++c, -1 != f.faces.indexOf(b.face)) {
                            N.remove(a, f);
                            a.unshift(f);
                            break
                        } var n = a.shift();
                    for (b = [n]; 0 < a.length;) c = Z.min(a, function(a) {
                        return I.distance(h.h[a.__id__], h.h[n.__id__])
                    }), n = I.distance(h.h[n.__id__],
                        h.h[c.__id__]) > I.distance(k, h.h[a[0].__id__]) ? a[0] : c, N.remove(a, n), b.push(n);
                    for (c = 0; c < b.length;) d = b[c], ++c, a.push(d)
                },
                __class__: ik
            };
            var Re = function(a) {
                this.district = a;
                switch (a.type._hx_index) {
                    case 1:
                        a = .1;
                        break;
                    case 3:
                        a = .1;
                        break;
                    case 4:
                        a = .1;
                        break;
                    case 5:
                        a = .5;
                        break;
                    default:
                        a = 1
                }
                this.rate = a
            };
            g["com.watabou.mfcg.model.Grower"] = Re;
            Re.__name__ = "com.watabou.mfcg.model.Grower";
            Re.prototype = {
                grow: function(a) {
                    if (0 == this.rate) return !1;
                    var b = 1 - this.rate;
                    null == b && (b = .5);
                    if ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 <
                        b) return !0;
                    for (var c = [], d = 0, f = this.district.faces; d < f.length;) {
                        var h = f[d];
                        ++d;
                        for (var k = h.halfEdge, n = k, p = !0; p;) {
                            var g = n;
                            n = n.next;
                            p = n != k;
                            b = g;
                            g = b.twin;
                            g = null != g ? g.face.data : null; - 1 != a.indexOf(g) && (b = this.validatePatch(h.data, g) * this.validateEdge(b.data), null == b && (b = .5), (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < b && c.push(g))
                        }
                    }
                    return 0 < c.length ? (c = Z.random(c), this.district.faces.push(c.face), c.district = this.district, N.remove(a, c), !0) : !1
                },
                validatePatch: function(a, b) {
                    return a.landing == b.landing ? 1 : 0
                },
                validateEdge: function(a) {
                    if (null != a) switch (a._hx_index) {
                        case 2:
                            return .9;
                        case 3:
                            return 0;
                        case 4:
                            return 0;
                        default:
                            return 1
                    } else return 1
                },
                __class__: Re
            };
            var ei = function(a) {
                Re.call(this, a)
            };
            g["com.watabou.mfcg.model.DocksGrower"] = ei;
            ei.__name__ = "com.watabou.mfcg.model.DocksGrower";
            ei.__super__ = Re;
            ei.prototype = v(Re.prototype, {
                validatePatch: function(a, b) {
                    return b.landing && b.ward instanceof Pc ? 1 : 0
                },
                __class__: ei
            });
            var fi = function(a) {
                Re.call(this, a)
            };
            g["com.watabou.mfcg.model.ParkGrower"] = fi;
            fi.__name__ = "com.watabou.mfcg.model.ParkGrower";
            fi.__super__ = Re;
            fi.prototype = v(Re.prototype, {
                validatePatch: function(a, b) {
                    return b.ward instanceof ce ? 1 : 0
                },
                __class__: fi
            });
            var pg = function() {
                this.components = []
            };
