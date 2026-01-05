/*
 * model-core/07-forester-landmark.js
 * Part 7/8: Forester and Landmark
 * Contains: Forester, Landmark classes
 */
            g["com.watabou.mfcg.model.Forester"] = Ae;
            Ae.__name__ = "com.watabou.mfcg.model.Forester";
            Ae.fillArea = function(a, b) {
                null == b && (b = 1);
                a = Ae.pattern.fill(new Yh(a));
                for (var c = [], d = 0; d < a.length;) {
                    var f = a[d];
                    ++d;
                    (Ae.noise.get(f.x, f.y) + 1) / 2 < b && c.push(f)
                }
                return c
            };
            Ae.fillLine = function(a, b, c) {
                null == c && (c = 1);
                for (var d = Math.ceil(I.distance(a, b) / 3), f = [], h = 0; h < d;) {
                    var k = h++;
                    k = qa.lerp(a,
                        b, (k + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / d);
                    (Ae.noise.get(k.x, k.y) + 1) / 2 < c && f.push(k)
                }
                return f
            };
            var di = function(a, b, c) {
                null == c && (c = "Landmark");
                this.model = a;
                this.pos = b;
                this.name = c;
                this.assign()
            };
            g["com.watabou.mfcg.model.Landmark"] = di;
            di.__name__ = "com.watabou.mfcg.model.Landmark";
            di.prototype = {
                assign: function() {
                    for (var a = 0, b = this.model.cells; a < b.length;) {
                        var c = b[a];
                        ++a;
                        if (this.assignPoly(c.shape)) break
                    }
                },
                assignPoly: function(a) {
                    if (Gb.rect(a).containsPoint(this.pos)) {
                        var b = a.length;
                        this.p0 =
                            a[0];
                        for (var c = 2; c < b;) {
                            var d = c++;
                            this.p1 = a[d - 1];
                            this.p2 = a[d];
                            d = qa.barycentric(this.p0, this.p1, this.p2, this.pos);
                            if (0 <= d.x && 0 <= d.y && 0 <= d.z) return this.i0 = d.x, this.i1 = d.y, this.i2 = d.z, !0
                        }
                    }
                    return !1
                },
                update: function() {
                    var a = this.p0,
                        b = this.i0,
                        c = new I(a.x * b, a.y * b);
                    a = this.p1;
                    b = this.i1;
                    c = c.add(new I(a.x * b, a.y * b));
                    a = this.p2;
                    b = this.i2;
                    this.pos = c.add(new I(a.x * b, a.y * b))
                },
                __class__: di
            };
            var je = function(a) {
                null == a && (a = []);
                this.valueClasses = a;
                this.slots = zd.NIL;
                this.priorityBased = !1
            };
            g["msignal.Signal"] = je;
            je.__name__ =
                "msignal.Signal";
            je.prototype = {
                add: function(a) {
                    return this.registerListener(a)
                },
                addOnce: function(a) {
                    return this.registerListener(a, !0)
                },
                addWithPriority: function(a, b) {
                    null == b && (b = 0);
                    return this.registerListener(a, !1, b)
                },
                addOnceWithPriority: function(a, b) {
                    null == b && (b = 0);
                    return this.registerListener(a, !0, b)
                },
                remove: function(a) {
                    var b = this.slots.find(a);
                    if (null == b) return null;
                    this.slots = this.slots.filterNot(a);
                    return b
                },
                removeAll: function() {
                    this.slots = zd.NIL
                },
                registerListener: function(a, b, c) {
                    null == c && (c =
                        0);
                    null == b && (b = !1);
                    return this.registrationPossible(a, b) ? (a = this.createSlot(a, b, c), this.priorityBased || 0 == c || (this.priorityBased = !0), this.slots = this.priorityBased || 0 != c ? this.slots.insertWithPriority(a) : this.slots.prepend(a), a) : this.slots.find(a)
                },
                registrationPossible: function(a, b) {
                    return this.slots.nonEmpty && null != this.slots.find(a) ? !1 : !0
                },
                createSlot: function(a, b, c) {
                    return null
                },
                get_numListeners: function() {
                    return this.slots.get_length()
                },
                __class__: je,
                __properties__: {
                    get_numListeners: "get_numListeners"
                }
            };
            var Nc = function() {
                je.call(this)
            };
            g["msignal.Signal0"] = Nc;
            Nc.__name__ = "msignal.Signal0";
            Nc.__super__ = je;
            Nc.prototype = v(je.prototype, {
                dispatch: function() {
                    for (var a = this.slots; a.nonEmpty;) a.head.execute(), a = a.tail
                },
                createSlot: function(a, b, c) {
                    null == c && (c = 0);
                    null == b && (b = !1);
                    return new gi(this, a, b, c)
                },
                __class__: Nc
            });
            var zd = function(a, b) {
                this.nonEmpty = !1;
                null == a && null == b ? this.nonEmpty = !1 : null != a && (this.head = a, this.tail = null == b ? zd.NIL : b, this.nonEmpty = !0)
            };
            g["msignal.SlotList"] = zd;
            zd.__name__ = "msignal.SlotList";
            zd.prototype = {
                get_length: function() {
                    if (!this.nonEmpty) return 0;
                    if (this.tail == zd.NIL) return 1;
                    for (var a = 0, b = this; b.nonEmpty;) ++a, b = b.tail;
                    return a
                },
                prepend: function(a) {
                    return new zd(a, this)
                },
                insertWithPriority: function(a) {
                    if (!this.nonEmpty) return new zd(a);
                    var b = a.priority;
                    if (b >= this.head.priority) return this.prepend(a);
                    for (var c = new zd(this.head), d = c, f = this.tail; f.nonEmpty;) {
                        if (b > f.head.priority) return d.tail = f.prepend(a), c;
                        d = d.tail = new zd(f.head);
                        f = f.tail
                    }
                    d.tail = new zd(a);
                    return c
                },
                filterNot: function(a) {
                    if (!this.nonEmpty ||
                        null == a) return this;
                    if (this.head.listener == a) return this.tail;
                    for (var b = new zd(this.head), c = b, d = this.tail; d.nonEmpty;) {
                        if (d.head.listener == a) return c.tail = d.tail, b;
                        c = c.tail = new zd(d.head);
                        d = d.tail
                    }
                    return this
                },
                find: function(a) {
                    if (!this.nonEmpty) return null;
                    for (var b = this; b.nonEmpty;) {
                        if (b.head.listener == a) return b.head;
                        b = b.tail
                    }
                    return null
                },
                __class__: zd,
                __properties__: {
                    get_length: "get_length"
                }
            };
            var ec = function(a) {
                je.call(this, [a])
            };
            g["msignal.Signal1"] = ec;
            ec.__name__ = "msignal.Signal1";
            ec.__super__ =
                je;
            ec.prototype = v(je.prototype, {
                dispatch: function(a) {
                    for (var b = this.slots; b.nonEmpty;) b.head.execute(a), b = b.tail
                },
                createSlot: function(a, b, c) {
                    null == c && (c = 0);
                    null == b && (b = !1);
                    return new hi(this, a, b, c)
                },
                __class__: ec
            });
            var Bb = function() {};
