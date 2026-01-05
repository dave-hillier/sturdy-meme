/*
 * model-core/08-model-misc.js
 * Part 8/8: Miscellaneous
 * Contains: ModelDispatcher, Topology, UnitSystem
 */
            g["com.watabou.mfcg.model.ModelDispatcher"] = Bb;
            Bb.__name__ = "com.watabou.mfcg.model.ModelDispatcher";
            var gh = function(a) {
                this.graph = new bk;
                this.pt2node = new pa;
                for (var b = 0; b < a.length;) {
                    var c = a[b];
                    ++b;
                    for (var d = c = c.face.halfEdge, f = !0; f;) {
                        var h = d;
                        d = d.next;
                        f = d != c;
                        if (null ==
                            h.data) {
                            var k = this.getNode(h.origin),
                                n = this.getNode(h.next.origin);
                            k.link(n, I.distance(h.origin.point, h.next.origin.point), !1)
                        }
                    }
                }
            };
            g["com.watabou.mfcg.model.Topology"] = gh;
            gh.__name__ = "com.watabou.mfcg.model.Topology";
            gh.prototype = {
                getNode: function(a) {
                    if (null != this.pt2node.h.__keys__[a.__id__]) return this.pt2node.h[a.__id__];
                    var b = this.pt2node,
                        c = this.graph.add(new ck(a));
                    b.set(a, c);
                    return c
                },
                buildPath: function(a, b) {
                    var c = this.pt2node.h[b.__id__];
                    if (null == this.pt2node.h[a.__id__] || null == c) return null;
                    a = this.graph.aStar(this.pt2node.h[a.__id__], this.pt2node.h[b.__id__]);
                    if (null == a) return null;
                    b = [];
                    for (c = 0; c < a.length;) {
                        var d = a[c];
                        ++c;
                        b.push(d.data)
                    }
                    return b
                },
                excludePoints: function(a) {
                    for (var b = 0; b < a.length;) {
                        var c = a[b];
                        ++b;
                        c = this.pt2node.h[c.__id__];
                        null != c && c.unlinkAll()
                    }
                },
                excludePolygon: function(a) {
                    for (var b = 0; b < a.length;) {
                        var c = a[b];
                        ++b;
                        var d = this.pt2node.h[c.origin.__id__];
                        c = this.pt2node.h[c.next.origin.__id__];
                        null != d && null != c && d.unlink(c)
                    }
                },
                __class__: gh
            };
            var Db = function(a, b, c) {
                this.unit = a;
                this.iu2unit = b;
                this.sub = c
            };
            g["com.watabou.mfcg.model.UnitSystem"] = Db;
            Db.__name__ = "com.watabou.mfcg.model.UnitSystem";
            Db.__properties__ = {
                set_current: "set_current",
                get_current: "get_current"
            };
            Db.toggle = function() {
                Db.set_current(Db.get_current() == Db.metric ? Db.imperial : Db.metric)
            };
            Db.get_current = function() {
                return Db._current
            };
            Db.set_current = function(a) {
                Db._current = a;
                Bb.unitsChanged.dispatch();
                return Db._current
            };
            Db.prototype = {
                measure: function(a) {
                    for (var b = this;;) {
                        var c = a / b.iu2unit;
                        if (1 > c && null != b.sub) b =
                            this.sub;
                        else return {
                            value: c,
                            system: b
                        }
                    }
                },
                getPlank: function(a, b) {
                    a = a.get_scaleX();
                    for (var c, d, f = this;;)
                        if (d = f.iu2unit * a, c = Math.pow(10, Math.ceil(Math.log(b / d) / Math.log(10))), d *= c, d > 5 * b ? c /= 5 : d > 4 * b ? c /= 4 : d > 2 * b && (c /= 2), 1 >= c && null != f.sub) f = f.sub;
                        else break;
                    return c * f.iu2unit
                },
                __class__: Db
            };
            var ii = function(a, b, c) {
                null == c && (c = !1);
                this.cacheOBB = new pa;
                this.cacheArea = new pa;
                this.group = a;
                this.shape = b;
                a = a.district.alleys;
                c ? this.lots = [b] : (ba.get("no_triangles", !1), ba.get("lots_method", "Twisted"), this.lots = $k.createLots(this,
                    a));
                sb.preview || "Offset" != ba.get("processing") || this.indentFronts(this.lots)
            };
