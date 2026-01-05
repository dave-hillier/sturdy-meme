/*
 * mfcg.js - Split Module 5/6: LZMA
 * LZMA compression algorithm implementation in JavaScript
 */
if ("undefined" === typeof self || !self.constructor.name.includes("Worker")) {
    var e = function() {
        function A(g) {
            var l = [];
            return l[g - 1] = void 0, l
        }

        function t(g, l) {
            return L(g[0] + l[0], g[1] + l[1])
        }

        function E(g, l) {
            var q = aa(g) & aa(l),
                u, r;
            return u = (~~Math.max(Math.min(g[1] / Ib, 2147483647), -2147483648) & ~~Math.max(Math.min(l[1] / Ib, 2147483647), -2147483648)) * Ib, r = q, 0 > q && (r += Ib), [r, u]
        }

        function B(g, l) {
            var q, u;
            return g[0] == l[0] && g[1] == l[1] ? 0 : (q = 0 > g[1], u = 0 > l[1], q && !u ? -1 : !q && u ? 1 : 0 > L(g[0] - l[0], g[1] - l[1])[1] ? -1 : 1)
        }

        function L(g,
            l) {
            l %= 1.8446744073709552E19;
            g %= 1.8446744073709552E19;
            var q = l % Ib;
            var u = Math.floor(g / Ib) * Ib;
            l = l - q + u;
            for (g = g - u + q; 0 > g;) g += Ib, l -= Ib;
            for (; 4294967295 < g;) g -= Ib, l += Ib;
            for (l %= 1.8446744073709552E19; 0x7fffffff00000000 < l;) l -= 1.8446744073709552E19;
            for (; - 9223372036854775808 > l;) l += 1.8446744073709552E19;
            return [g, l]
        }

        function M(g) {
            return 0 <= g ? [g, 0] : [g + Ib, -Ib]
        }

        function aa(g) {
            return 2147483648 <= g[0] ? ~~Math.max(Math.min(g[0] - Ib, 2147483647), -2147483648) : ~~Math.max(Math.min(g[0], 2147483647), -2147483648)
        }

        function v(g) {
            return 30 >=
                g ? 1 << g : v(30) * v(g - 30)
        }

        function ea(g, l) {
            var q, u, r, t;
            if (l &= 63, g[0] == ff[0] && g[1] == ff[1]) return l ? ta : g;
            if (0 > g[1]) throw Error("Neg");
            return t = v(l), u = g[1] * t % 1.8446744073709552E19, r = g[0] * t, q = r - r % Ib, u += q, r -= q, 0x7fffffffffffffff <= u && (u -= 1.8446744073709552E19), [r, u]
        }

        function l(g, l) {
            var q;
            return l &= 63, q = v(l), L(Math.floor(g[0] / q), g[1] / q)
        }

        function g(g, l) {
            return g.Mc = l, g.Lc = 0, g.Yb = l.length, g
        }

        function r(g) {
            return g.Lc >= g.Yb ? -1 : 255 & g.Mc[g.Lc++]
        }

        function y(g) {
            return g.Mc = A(32), g.Yb = 0, g
        }

        function G(g) {
            var l = g.Mc;
            return l.length =
                g.Yb, l
        }

        function Za(g, l, r, t, v) {
            for (var q = 0; v > q; ++q) r[t + q] = g[l + q]
        }

        function sa(q, u, r) {
            q.Nb = y({});
            var t = g({}, u),
                v = q.Nb,
                w = M(u.length);
            if (0 > B(w, dd)) throw Error("invalid length " + w);
            q.Tb = w;
            u = {};
            var D;
            u.v = A(4);
            u.a = [];
            u.d = {};
            u.C = A(192);
            u.bb = A(12);
            u.hb = A(12);
            u.Ub = A(12);
            u.vc = A(12);
            u._ = A(192);
            u.K = [];
            u.Sb = A(114);
            u.S = Cb({}, 4);
            u.$ = H({});
            u.i = H({});
            u.A = {};
            u.m = [];
            u.P = [];
            u.lb = [];
            u.nc = A(16);
            u.x = A(4);
            u.Q = A(4);
            u.Xb = [ta];
            u.uc = [ta];
            u.Kc = [0];
            u.fc = A(5);
            u.yc = A(128);
            u.vb = 0;
            u.X = 1;
            u.D = 0;
            u.Hb = -1;
            for (D = u.mb = 0; 4096 > D; ++D) u.a[D] = {};
            for (D = 0; 4 > D; ++D) u.K[D] = Cb({}, 6);
            D = 1 << r.s;
            u.ab = D;
            for (var x = 0; D > 1 << x; ++x);
            u.$b = 2 * x;
            u.n = r.f;
            D = u.X;
            u.X = r.m;
            u.b && D != u.X && (u.wb = -1, u.b = null);
            u.eb = 0;
            u.fb = 3;
            u.Y = 2;
            u.y = 3;
            u.Gc = void 0 === e.disableEndMark;
            u.fc[0] = 9 * (5 * u.Y + u.eb) + u.fb << 24 >> 24;
            for (r = 0; 4 > r; ++r) u.fc[1 + r] = u.ab >> 8 * r << 24 >> 24;
            Za(u.fc, 0, v.Mc, v.Yb, 5);
            v.Yb += 5;
            for (r = 0; 64 > r; r += 8) D = 255 & aa(l(w, r)), v.Mc[v.Yb++] = D << 24 >> 24;
            u.W = 0;
            u.oc = t;
            u.pc = 0;
            u.b || (t = {}, w = 4, u.X || (w = 2), t.qb = 2 < w, t.qb ? (t.w = 0, t.xb = 4, t.R = 66560) : (t.w = 2, t.xb = 3, t.R = 0), u.b = t);
            t = u.A;
            w = u.eb;
            r = u.fb;
            if (null ==
                t.V || t.u != r || t.I != w)
                for (t.I = w, t.qc = (1 << w) - 1, t.u = r, r = 1 << t.u + t.I, t.V = A(r), w = 0; r > w; ++w) {
                    D = t.V;
                    x = w;
                    var E = {};
                    E = (E.tb = A(768), E);
                    D[x] = E
                }
            if (u.ab != u.wb || u.Hb != u.n) t = u.b, w = u.ab, r = u.n, 1073741567 > w && (t.Fc = 16 + (r >> 1), x = w + 4096, D = r + 274, t.Bc = x, t._b = D, x = x + D + (~~((w + 4096 + r + 274) / 2) + 256), (null == t.c || t.Kb != x) && (t.c = null, t.Kb = x, t.c = A(t.Kb)), t.H = t.Kb - D, t.ob = r, r = w + 1, t.p != r && (t.L = A(2 * (t.p = r))), r = 65536, t.qb && (r = w - 1, r |= r >> 1, r |= r >> 2, r |= r >> 4, r |= r >> 8, r >>= 1, r |= 65535, 16777216 < r && (r >>= 1), t.Ec = r, ++r, r += t.R), r != t.rc && (t.ub = A(t.rc = r))),
                u.wb = u.ab, u.Hb = u.n;
            u.d.Ab = v;
            u.l = 0;
            for (v = u.J = 0; 4 > v; ++v) u.v[v] = 0;
            v = u.d;
            v.mc = ta;
            v.xc = ta;
            v.E = -1;
            v.Jb = 1;
            v.Oc = 0;
            fb(u.C);
            fb(u._);
            fb(u.bb);
            fb(u.hb);
            fb(u.Ub);
            fb(u.vc);
            fb(u.Sb);
            v = u.A;
            w = 1 << v.u + v.I;
            for (t = 0; w > t; ++t) fb(v.V[t].tb);
            for (v = 0; 4 > v; ++v) fb(u.K[v].G);
            T(u.$, 1 << u.Y);
            T(u.i, 1 << u.Y);
            fb(u.S.G);
            u.N = 0;
            u.jb = 0;
            u.q = 0;
            u.s = 0;
            F(u);
            N(u);
            u.$.rb = u.n + 1 - 2;
            wb(u.$, 1 << u.Y);
            u.i.rb = u.n + 1 - 2;
            wb(u.i, 1 << u.Y);
            void(u.g = ta);
            v = {};
            u = (v.cb = u, v.Z = null, v.zc = 1, v);
            q.yb = u;
            return q
        }

        function $a(l, u) {
            l.Nb = y({});
            var q = g({}, u),
                t = l.Nb,
                v, x, B, F = "",
                E = [];
            for (v = 0; 5 > v; ++v) {
                if (x = r(q), -1 == x) throw Error("truncated input");
                E[v] = x << 24 >> 24
            }
            u = {
                B: {},
                e: {}
            };
            u.Gb = A(192);
            u.Zb = A(12);
            u.Cb = A(12);
            u.Db = A(12);
            u.Eb = A(12);
            u.pb = A(192);
            u.kb = A(4);
            u.kc = A(114);
            u.Fb = w({}, 4);
            u.Rb = sb({});
            u.sb = sb({});
            u.gb = {};
            for (v = 0; 4 > v; ++v) u.kb[v] = w({}, 6);
            var G;
            if (5 > E.length) v = 0;
            else {
                v = 255 & E[0];
                var H = v % 9;
                v = ~~(v / 9);
                var J = v % 5;
                var L = ~~(v / 5);
                for (G = v = 0; 4 > G; ++G) v += (255 & E[1 + G]) << 8 * G;
                if (!(E = 99999999 < v)) {
                    if (8 < H || 4 < J || 4 < L) L = 0;
                    else {
                        E = u.gb;
                        if (null == E.V || E.u != H || E.I != J)
                            for (E.I = J, E.qc = (1 << J) - 1, E.u = H, J =
                                1 << E.u + E.I, E.V = A(J), H = 0; J > H; ++H) {
                                G = E.V;
                                var N = H;
                                var O = {};
                                O = (O.Ib = A(768), O);
                                G[N] = O
                            }
                        L = 1 << L;
                        L = (ka(u.Rb, L), ka(u.sb, L), u.Dc = L - 1, 1)
                    }
                    E = !L
                }
                E ? v = 0 : 0 > v ? v = 0 : (u.Ob != v && (u.Ob = v, u.nb = Math.max(u.Ob, 1), v = u.B, L = Math.max(u.nb, 4096), null != v.Lb && v.M == L || (v.Lb = A(L)), v.M = L, v.o = 0, v.h = 0), v = 1)
            }
            if (!v) throw Error("corrupted input");
            for (v = 0; 64 > v; v += 8) {
                if (x = r(q), -1 == x) throw Error("truncated input");
                x = x.toString(16);
                1 == x.length && (x = "0" + x);
                F = x + "" + F
            }
            /^0+$|^f+$/i.test(F) ? l.Tb = dd : (B = parseInt(F, 16), l.Tb = 4294967295 < B ? dd : M(B));
            x = l.Tb;
            u.e.Ab = q;
            q = u.B;
            pa(q);
            q.cc = null;
            u.B.cc = t;
            u.B.h = 0;
            u.B.o = 0;
            fb(u.Gb);
            fb(u.pb);
            fb(u.Zb);
            fb(u.Cb);
            fb(u.Db);
            fb(u.Eb);
            fb(u.kc);
            t = u.gb;
            B = 1 << t.u + t.I;
            for (q = 0; B > q; ++q) fb(t.V[q].Ib);
            for (t = 0; 4 > t; ++t) fb(u.kb[t].G);
            rc(u.Rb);
            rc(u.sb);
            fb(u.Fb.G);
            t = u.e;
            t.Bb = 0;
            t.E = -1;
            for (q = 0; 5 > q; ++q) t.Bb = t.Bb << 8 | r(t.Ab);
            u.U = 0;
            u.ib = 0;
            u.Jc = 0;
            u.Ic = 0;
            u.Qc = 0;
            u.Nc = x;
            u.g = ta;
            u.jc = 0;
            t = {};
            u = (t.Z = u, t.cb = null, t.zc = 1, t);
            l.yb = u;
            return l
        }

        function Pa(g, l) {
            return g.c[g.f + g.o + l]
        }

        function Sb(g, l, r, t) {
            g.T && g.o + l + t > g.h && (t = g.h - (g.o + l));
            ++r;
            var q = g.f + g.o +
                l;
            for (l = 0; t > l && g.c[q + l] == g.c[q + l - r]; ++l);
            return l
        }

        function ob(g) {
            return g.h - g.o
        }

        function oa(g) {
            var l, q, r;
            if (!g.T)
                for (; r = -g.f + g.Kb - g.h, r;) {
                    var t = g.cc,
                        v = r;
                    if (l = t.Lc >= t.Yb ? -1 : (v = Math.min(v, t.Yb - t.Lc), Za(t.Mc, t.Lc, g.c, g.f + g.h, v), t.Lc += v, v), -1 == l) return g.zb = g.h, q = g.f + g.zb, q > g.H && (g.zb = g.H - g.f), void(g.T = 1);
                    g.h += l;
                    g.h >= g.o + g._b && (g.zb = g.h - g._b)
                }
        }

        function mb(g, l) {
            g.f += l;
            g.zb -= l;
            g.o -= l;
            g.h -= l
        }

        function la(g) {
            var l;
            ++g.k >= g.p && (g.k = 0);
            ++g.o;
            if (g.o > g.zb) {
                var q = g.f + g.o;
                if (q > g.H) {
                    var r = g.f + g.o - g.Bc;
                    0 < r && --r;
                    var t =
                        g.f + g.h - r;
                    for (q = 0; t > q; ++q) g.c[q] = g.c[r + q];
                    g.f -= r
                }
                oa(g)
            }
            1073741823 == g.o && (l = g.o - g.p, nb(g.L, 2 * g.p, l), nb(g.ub, g.rc, l), mb(g, l))
        }

        function nb(g, l, r) {
            var q;
            for (q = 0; l > q; ++q) {
                var u = g[q] || 0;
                r >= u ? u = 0 : u -= r;
                g[q] = u
            }
        }

        function Y(g, l) {
            var q, u, r, t, v, w, x, B, y, A;
            do {
                if (g.h >= g.o + g.ob) var E = g.ob;
                else if (E = g.h - g.o, g.xb > E) {
                    la(g);
                    continue
                }
                var F = g.o > g.p ? g.o - g.p : 0;
                var G = g.f + g.o;
                g.qb ? (A = fe[255 & g.c[G]] ^ 255 & g.c[G + 1], t = 1023 & A, g.ub[t] = g.o, A ^= (255 & g.c[G + 2]) << 8, v = 65535 & A, g.ub[1024 + v] = g.o, w = (A ^ fe[255 & g.c[G + 3]] << 5) & g.Ec) : w = 255 & g.c[G] ^
                    (255 & g.c[G + 1]) << 8;
                var H = g.ub[g.R + w];
                g.ub[g.R + w] = g.o;
                var J = (g.k << 1) + 1;
                var L = g.k << 1;
                var M = B = g.w;
                for (q = g.Fc;;) {
                    if (F >= H || 0 == q--) {
                        g.L[J] = g.L[L] = 0;
                        break
                    }
                    if (r = g.o - H, u = (g.k >= r ? g.k - r : g.k - r + g.p) << 1, y = g.f + H, x = B > M ? M : B, g.c[y + x] == g.c[G + x]) {
                        for (; ++x != E && g.c[y + x] == g.c[G + x];);
                        if (x == E) {
                            g.L[L] = g.L[u];
                            g.L[J] = g.L[u + 1];
                            break
                        }
                    }(255 & g.c[G + x]) > (255 & g.c[y + x]) ? (g.L[L] = H, L = u + 1, H = g.L[L], B = x) : (g.L[J] = H, J = u, H = g.L[J], M = x)
                }
                la(g)
            } while (0 != --l)
        }

        function pa(g) {
            var l = g.o - g.h;
            if (l) {
                var q = g.cc;
                Za(g.Lb, g.h, q.Mc, q.Yb, l);
                q.Yb += l;
                g.o >= g.M &&
                    (g.o = 0);
                g.h = g.o
            }
        }

        function ab(g, l) {
            l = g.o - l - 1;
            return 0 > l && (l += g.M), g.Lb[l]
        }

        function S(g) {
            return g -= 2, 4 > g ? g : 3
        }

        function xa(g) {
            return 4 > g ? 0 : 10 > g ? g - 3 : g - 6
        }

        function kb(g) {
            if (!g.zc) throw Error("bad state");
            if (g.cb) {
                a: {
                    var l = g.cb,
                        q = g.cb.Xb,
                        v = g.cb.uc,
                        w = g.cb.Kc,
                        y, A;q[0] = ta;v[0] = ta;w[0] = 1;
                    if (l.oc) {
                        l.b.cc = l.oc;
                        var E = l.b;
                        E.f = 0;
                        E.o = 0;
                        E.h = 0;
                        E.T = 0;
                        oa(E);
                        E.k = 0;
                        mb(E, -1);
                        l.W = 1;
                        l.oc = null
                    }
                    if (!l.pc) {
                        l.pc = 1;
                        var G = E = l.g;
                        if (G[0] == ta[0] && G[1] == ta[1]) {
                            if (!ob(l.b)) {
                                Ab(l, aa(l.g));
                                break a
                            }
                            Na(l);
                            var H = aa(l.g) & l.y;
                            gb(l.d, l.C, (l.l <<
                                4) + H, 0);
                            l.l = xa(l.l);
                            G = Pa(l.b, -l.s);
                            O(x(l.A, aa(l.g), l.J), l.d, G);
                            l.J = G;
                            --l.s;
                            l.g = t(l.g, Je)
                        }
                        if (ob(l.b))
                            for (;;) {
                                if (y = ib(l, aa(l.g)), A = l.mb, H = aa(l.g) & l.y, G = (l.l << 4) + H, 1 == y && -1 == A) {
                                    gb(l.d, l.C, G, 0);
                                    G = Pa(l.b, -l.s);
                                    var J = x(l.A, aa(l.g), l.J);
                                    if (7 > l.l) O(J, l.d, G);
                                    else {
                                        var T = Pa(l.b, -l.v[0] - 1 - l.s);
                                        var Y = void 0;
                                        var ea;
                                        var ja = l.d;
                                        var ka = T,
                                            la = G,
                                            sa = 1,
                                            Aa = 1;
                                        for (ea = 7; 0 <= ea; --ea) {
                                            var Ia = la >> ea & 1;
                                            T = Aa;
                                            sa && (Y = ka >> ea & 1, T += 1 + Y << 8, sa = Y == Ia);
                                            gb(ja, J.tb, T, Ia);
                                            Aa = Aa << 1 | Ia
                                        }
                                    }
                                    l.J = G;
                                    l.l = xa(l.l)
                                } else {
                                    if (gb(l.d, l.C, G, 1), 4 > A) {
                                        if (gb(l.d,
                                                l.bb, l.l, 1), A ? (gb(l.d, l.hb, l.l, 1), 1 == A ? gb(l.d, l.Ub, l.l, 0) : (gb(l.d, l.Ub, l.l, 1), gb(l.d, l.vc, l.l, A - 2))) : (gb(l.d, l.hb, l.l, 0), 1 == y ? gb(l.d, l._, G, 0) : gb(l.d, l._, G, 1)), 1 == y ? l.l = 7 > l.l ? 9 : 11 : (ya(l.i, l.d, y - 2, H), l.l = 7 > l.l ? 8 : 11), G = l.v[A], 0 != A) {
                                            for (Y = A; 1 <= Y; --Y) l.v[Y] = l.v[Y - 1];
                                            l.v[0] = G
                                        }
                                    } else {
                                        gb(l.d, l.bb, l.l, 0);
                                        l.l = 7 > l.l ? 7 : 10;
                                        ya(l.$, l.d, y - 2, H);
                                        A -= 4;
                                        Y = ca(A);
                                        G = S(y);
                                        W(l.K[G], l.d, Y);
                                        if (4 <= Y)
                                            if (ja = (Y >> 1) - 1, J = (2 | 1 & Y) << ja, T = A - J, 14 > Y)
                                                for (G = l.Sb, Y = J - Y - 1, J = l.d, Ia = T, ka = 1, T = 0; ja > T; ++T) ea = 1 & Ia, gb(J, G, Y + ka, ea), ka = ka << 1 | ea, Ia >>=
                                                    1;
                                            else oc(l.d, T >> 4, ja - 4), U(l.S, l.d, 15 & T), ++l.Qb;
                                        G = A;
                                        for (Y = 3; 1 <= Y; --Y) l.v[Y] = l.v[Y - 1];
                                        l.v[0] = G;
                                        ++l.Mb
                                    }
                                    l.J = Pa(l.b, y - 1 - l.s)
                                }
                                if (l.s -= y, l.g = t(l.g, M(y)), !l.s) {
                                    128 <= l.Mb && F(l);
                                    16 <= l.Qb && N(l);
                                    q[0] = l.g;
                                    G = l.d;
                                    G = t(t(M(G.Jb), G.mc), [4, 0]);
                                    if (v[0] = G, !ob(l.b)) {
                                        Ab(l, aa(l.g));
                                        break a
                                    }
                                    G = l.g;
                                    G = L(G[0] - E[0], G[1] - E[1]);
                                    if (0 <= B(G, [4096, 0])) {
                                        l.pc = 0;
                                        w[0] = 0;
                                        break a
                                    }
                                }
                            } else Ab(l, aa(l.g))
                    }
                }
                g.Pb = g.cb.Xb[0];g.cb.Kc[0] && (y = g.cb, ha(y), y.d.Ab = null, g.zc = 0)
            }
            else {
                a: {
                    y = g.Z;
                    if (Y = aa(y.g) & y.Dc, Hb(y.e, y.Gb, (y.U << 4) + Y)) {
                        if (Hb(y.e, y.Zb, y.U)) l =
                            0, Hb(y.e, y.Cb, y.U) ? (Hb(y.e, y.Db, y.U) ? (Hb(y.e, y.Eb, y.U) ? (G = y.Qc, y.Qc = y.Ic) : G = y.Ic, y.Ic = y.Jc) : G = y.Jc, y.Jc = y.ib, y.ib = G) : Hb(y.e, y.pb, (y.U << 4) + Y) || (y.U = 7 > y.U ? 9 : 11, l = 1), l || (l = bb(y.sb, y.e, Y) + 2, y.U = 7 > y.U ? 8 : 11);
                        else if (y.Qc = y.Ic, y.Ic = y.Jc, y.Jc = y.ib, l = 2 + bb(y.Rb, y.e, Y), y.U = 7 > y.U ? 7 : 10, E = cb(y.kb[S(l)], y.e), 4 <= E)
                            if (q = (E >> 1) - 1, y.ib = (2 | 1 & E) << q, 14 > E) {
                                A = y.ib;
                                H = y.kc;
                                v = y.ib - E - 1;
                                w = y.e;
                                Y = 1;
                                for (G = J = 0; q > G; ++G) E = Hb(w, H, v + Y), Y <<= 1, Y += E, J |= E << G;
                                y.ib = A + J
                            } else {
                                A = y.ib;
                                H = y.e;
                                v = 0;
                                for (q -= 4; 0 != q; --q) H.E >>>= 1, w = H.Bb - H.E >>> 31, H.Bb -=
                                    H.E & w - 1, v = v << 1 | 1 - w, -16777216 & H.E || (H.Bb = H.Bb << 8 | r(H.Ab), H.E <<= 8);
                                y.ib = A + (v << 4);
                                A = y.ib;
                                H = y.Fb;
                                q = y.e;
                                E = 1;
                                for (w = G = 0; H.F > w; ++w) v = Hb(q, H.G, E), E <<= 1, E += v, G |= v << w;
                                if (y.ib = A + G, 0 > y.ib) {
                                    y = -1 == y.ib ? 1 : -1;
                                    break a
                                }
                            }
                        else y.ib = E;
                        if (0 <= B(M(y.ib), y.g) || y.ib >= y.nb) {
                            y = -1;
                            break a
                        }
                        A = y.B;
                        H = l;
                        q = A.o - y.ib - 1;
                        for (0 > q && (q += A.M); 0 != H; --H) q >= A.M && (q = 0), A.Lb[A.o++] = A.Lb[q++], A.o >= A.M && pa(A);
                        y.g = t(y.g, M(l));
                        y.jc = ab(y.B, 0)
                    } else {
                        A = y.gb;
                        H = aa(y.g);
                        A = A.V[((H & A.qc) << A.u) + ((255 & y.jc) >>> 8 - A.u)];
                        if (7 > y.U) {
                            H = y.e;
                            l = 1;
                            do l = l << 1 | Hb(H, A.Ib, l);
                            while (256 > l);
                            A = l << 24 >> 24
                        } else {
                            H = y.e;
                            l = ab(y.B, y.ib);
                            q = 1;
                            do
                                if (w = l >> 7 & 1, l <<= 1, v = Hb(H, A.Ib, (1 + w << 8) + q), q = q << 1 | v, w != v) {
                                    for (; 256 > q;) q = q << 1 | Hb(H, A.Ib, q);
                                    break
                                } while (256 > q);
                            A = q << 24 >> 24
                        }
                        y.jc = A;
                        A = y.B;
                        H = y.jc;
                        A.Lb[A.o++] = H;
                        A.o >= A.M && pa(A);
                        y.U = xa(y.U);
                        y.g = t(y.g, Je)
                    }
                    y = 0
                }
                if (-1 == y) throw Error("corrupted input");g.Pb = dd;g.Pc = g.Z.g;
                if (y || 0 <= B(g.Z.Nc, ta) && 0 <= B(g.Z.g, g.Z.Nc)) pa(g.Z.B),
                y = g.Z.B,
                pa(y),
                y.cc = null,
                g.Z.e.Ab = null,
                g.zc = 0
            }
            return g.zc
        }

        function ka(g, l) {
            for (; l > g.O; ++g.O) g.ec[g.O] = w({}, 3), g.hc[g.O] = w({}, 3)
        }

        function bb(g,
            l, r) {
            return Hb(l, g.wc, 0) ? 8 + (Hb(l, g.wc, 1) ? 8 + cb(g.tc, l) : cb(g.hc[r], l)) : cb(g.ec[r], l)
        }

        function sb(g) {
            return g.wc = A(2), g.ec = A(16), g.hc = A(16), g.tc = w({}, 8), g.O = 0, g
        }

        function rc(g) {
            fb(g.wc);
            for (var l = 0; g.O > l; ++l) fb(g.ec[l].G), fb(g.hc[l].G);
            fb(g.tc.G)
        }

        function ja(g, l) {
            g.jb = l;
            var q = g.a[l].r;
            var r = g.a[l].j;
            do {
                if (g.a[l].t) {
                    var u = g.a[q];
                    u.j = -1;
                    u.t = 0;
                    g.a[q].r = q - 1;
                    g.a[l].Ac && (g.a[q - 1].t = 0, g.a[q - 1].r = g.a[l].r2, g.a[q - 1].j = g.a[l].j2)
                }
                var t = q;
                u = r;
                r = g.a[t].j;
                q = g.a[t].r;
                g.a[t].j = u;
                g.a[t].r = l;
                l = t
            } while (0 < l);
            return g.mb =
                g.a[0].j, g.q = g.a[0].r
        }

        function N(g) {
            for (var l = 0; 16 > l; ++l) {
                var q = g.nc,
                    r = l,
                    t, v = g.S,
                    w = l,
                    x = 1,
                    y = 0;
                for (t = v.F; 0 != t; --t) {
                    var A = 1 & w;
                    w >>>= 1;
                    y += ud(v.G[x], A);
                    x = x << 1 | A
                }
                q[r] = y
            }
            g.Qb = 0
        }

        function F(g) {
            var l;
            for (l = 4; 128 > l; ++l) {
                var q = ca(l);
                var r = (q >> 1) - 1;
                var t = (2 | 1 & q) << r;
                var v = g.yc;
                for (var w = l, x, y = l - t, A = 1, B = 0; 0 != r; --r) x = 1 & y, y >>>= 1, B += Jb[(2047 & (g.Sb[t - q - 1 + A] - x ^ -x)) >>> 2], A = A << 1 | x;
                v[w] = B
            }
            for (t = 0; 4 > t; ++t) {
                l = g.K[t];
                v = t << 6;
                for (q = 0; g.$b > q; ++q) g.P[v + q] = Pb(l, q);
                for (q = 14; g.$b > q; ++q) g.P[v + q] += (q >> 1) - 1 - 4 << 6;
                q = 128 * t;
                for (l = 0; 4 > l; ++l) g.lb[q +
                    l] = g.P[v + l];
                for (; 128 > l; ++l) g.lb[q + l] = g.P[v + ca(l)] + g.yc[l]
            }
            g.Mb = 0
        }

        function Ab(g, l) {
            ha(g);
            l &= g.y;
            g.Gc && (gb(g.d, g.C, (g.l << 4) + l, 1), gb(g.d, g.bb, g.l, 0), g.l = 7 > g.l ? 7 : 10, ya(g.$, g.d, 0, l), l = S(2), W(g.K[l], g.d, 63), oc(g.d, 67108863, 26), U(g.S, g.d, 15));
            for (l = 0; 5 > l; ++l) ic(g.d)
        }

        function ib(g, l) {
            var q, r, u, t, v, w, y, A, B, E, F, G, H, L, M, N, O, S, T, W, aa, U, ca, ea, ha, ka, oa, pa;
            if (g.jb != g.q) return E = g.a[g.q].r - g.q, g.mb = g.a[g.q].j, g.q = g.a[g.q].r, E;
            if (g.q = g.jb = 0, g.N ? (B = g.vb, g.N = 0) : B = Na(g), E = g.D, O = ob(g.b) + 1, 2 > O) return g.mb = -1, 1;
            273 < O &&
                (O = 273);
            for (w = y = 0; 4 > w; ++w) g.x[w] = g.v[w], g.Q[w] = Sb(g.b, -1, g.x[w], 273), g.Q[w] > g.Q[y] && (y = w);
            if (g.Q[y] >= g.n) return g.mb = y, E = g.Q[y], l = E - 1, 0 < l && (Y(g.b, l), g.s += l), E;
            if (B >= g.n) return g.mb = g.m[E - 1] + 4, l = B - 1, 0 < l && (Y(g.b, l), g.s += l), B;
            if (v = Pa(g.b, -1), L = Pa(g.b, -g.v[0] - 1 - 1), 2 > B && v != L && 2 > g.Q[y]) return g.mb = -1, 1;
            g.a[0].Hc = g.l;
            var la = l & g.y;
            g.a[1].z = Jb[g.C[(g.l << 4) + la] >>> 2] + J(x(g.A, l, g.J), 7 <= g.l, L, v);
            var I = g.a[1];
            I.j = -1;
            I.t = 0;
            I = Jb[2048 - g.C[(g.l << 4) + la] >>> 2];
            var sa = I + Jb[2048 - g.bb[g.l] >>> 2];
            if (L == v) {
                var ya = g.l;
                ya = sa +
                    (Jb[g.hb[ya] >>> 2] + Jb[g._[(ya << 4) + la] >>> 2]);
                g.a[1].z > ya && (g.a[1].z = ya, w = g.a[1], w.j = 0, w.t = 0)
            }
            if (A = B >= g.Q[y] ? B : g.Q[y], 2 > A) return g.mb = g.a[1].j, 1;
            g.a[1].r = 0;
            g.a[0].bc = g.x[0];
            g.a[0].ac = g.x[1];
            g.a[0].dc = g.x[2];
            g.a[0].lc = g.x[3];
            y = A;
            do g.a[y--].z = 268435455; while (2 <= y);
            for (w = 0; 4 > w; ++w)
                if (H = g.Q[w], !(2 > H)) {
                    var Ja = sa + Ia(g, w, g.l, la);
                    do {
                        var ta = Ja + g.i.Cc[272 * la + (H - 2)];
                        var Ea = g.a[H];
                        Ea.z > ta && (Ea.z = ta, Ea.r = 0, Ea.j = w, Ea.t = 0)
                    } while (2 <= --H)
                } if (H = I + Jb[g.bb[g.l] >>> 2], y = 2 <= g.Q[0] ? g.Q[0] + 1 : 2, B >= y) {
                for (B = 0; y > g.m[B];) B += 2;
                for (; q = g.m[B + 1], ta = H + Aa(g, q, y, la), Ea = g.a[y], Ea.z > ta && (Ea.z = ta, Ea.r = 0, Ea.j = q + 4, Ea.t = 0), y != g.m[B] || (B += 2, B != E); ++y);
            }
            for (q = 0;;) {
                if (++q, q == A) return ja(g, q);
                if (M = Na(g), E = g.D, M >= g.n) return g.vb = M, g.N = 1, ja(g, q);
                if (++l, aa = g.a[q].r, g.a[q].t ? (--aa, g.a[q].Ac ? (U = g.a[g.a[q].r2].Hc, U = 4 > g.a[q].j2 ? 7 > U ? 8 : 11 : 7 > U ? 7 : 10) : U = g.a[aa].Hc, U = xa(U)) : U = g.a[aa].Hc, aa == q - 1 ? U = g.a[q].j ? xa(U) : 7 > U ? 9 : 11 : (g.a[q].t && g.a[q].Ac ? (aa = g.a[q].r2, W = g.a[q].j2, U = 7 > U ? 8 : 11) : (W = g.a[q].j, U = 4 > W ? 7 > U ? 8 : 11 : 7 > U ? 7 : 10), T = g.a[aa], 4 > W ? W ? 1 == W ? (g.x[0] = T.ac,
                        g.x[1] = T.bc, g.x[2] = T.dc, g.x[3] = T.lc) : 2 == W ? (g.x[0] = T.dc, g.x[1] = T.bc, g.x[2] = T.ac, g.x[3] = T.lc) : (g.x[0] = T.lc, g.x[1] = T.bc, g.x[2] = T.ac, g.x[3] = T.dc) : (g.x[0] = T.bc, g.x[1] = T.ac, g.x[2] = T.dc, g.x[3] = T.lc) : (g.x[0] = W - 4, g.x[1] = T.bc, g.x[2] = T.ac, g.x[3] = T.dc)), g.a[q].Hc = U, g.a[q].bc = g.x[0], g.a[q].ac = g.x[1], g.a[q].dc = g.x[2], g.a[q].lc = g.x[3], t = g.a[q].z, v = Pa(g.b, -1), L = Pa(g.b, -g.x[0] - 1 - 1), la = l & g.y, r = t + Jb[g.C[(U << 4) + la] >>> 2] + J(x(g.A, l, Pa(g.b, -2)), 7 <= U, L, v), N = g.a[q + 1], B = 0, N.z > r && (N.z = r, N.r = q, N.j = -1, N.t = 0, B = 1), I = t + Jb[2048 -
                        g.C[(U << 4) + la] >>> 2], sa = I + Jb[2048 - g.bb[U] >>> 2], L != v || q > N.r && !N.j || (ya = sa + (Jb[g.hb[U] >>> 2] + Jb[g._[(U << 4) + la] >>> 2]), N.z >= ya && (N.z = ya, N.r = q, N.j = 0, N.t = 0, B = 1)), S = ob(g.b) + 1, S = S > 4095 - q ? 4095 - q : S, O = S, !(2 > O)) {
                    if (O > g.n && (O = g.n), !B && L != v && (ca = Math.min(S - 1, g.n), G = Sb(g.b, 0, g.x[0], ca), 2 <= G)) {
                        w = xa(U);
                        Ea = l + 1 & g.y;
                        ta = r + Jb[2048 - g.C[(w << 4) + Ea] >>> 2] + Jb[2048 - g.bb[w] >>> 2];
                        for (Ja = q + 1 + G; Ja > A;) g.a[++A].z = 268435455;
                        ta += (ea = g.i.Cc[272 * Ea + (G - 2)], ea + Ia(g, 0, w, Ea));
                        Ea = g.a[Ja];
                        Ea.z > ta && (Ea.z = ta, Ea.r = q + 1, Ea.j = 0, Ea.t = 1, Ea.Ac = 0)
                    }
                    y = 2;
                    for (B = 0; 4 > B; ++B)
                        if (F = Sb(g.b, -1, g.x[B], O), !(2 > F)) {
                            H = F;
                            do {
                                for (; q + F > A;) g.a[++A].z = 268435455;
                                ta = sa + (ha = g.i.Cc[272 * la + (F - 2)], ha + Ia(g, B, U, la));
                                Ea = g.a[q + F];
                                Ea.z > ta && (Ea.z = ta, Ea.r = q, Ea.j = B, Ea.t = 0)
                            } while (2 <= --F);
                            if (F = H, B || (y = F + 1), S > F && (ca = Math.min(S - 1 - F, g.n), G = Sb(g.b, F, g.x[B], ca), 2 <= G)) {
                                w = 7 > U ? 8 : 11;
                                Ea = l + F & g.y;
                                ta = sa + (ka = g.i.Cc[272 * la + (F - 2)], ka + Ia(g, B, U, la)) + Jb[g.C[(w << 4) + Ea] >>> 2] + J(x(g.A, l + F, Pa(g.b, F - 1 - 1)), 1, Pa(g.b, F - 1 - (g.x[B] + 1)), Pa(g.b, F - 1));
                                w = xa(w);
                                Ea = l + F + 1 & g.y;
                                ta += Jb[2048 - g.C[(w << 4) + Ea] >>> 2];
                                ta += Jb[2048 -
                                    g.bb[w] >>> 2];
                                for (Ja = F + 1 + G; q + Ja > A;) g.a[++A].z = 268435455;
                                ta += (oa = g.i.Cc[272 * Ea + (G - 2)], oa + Ia(g, 0, w, Ea));
                                Ea = g.a[q + Ja];
                                Ea.z > ta && (Ea.z = ta, Ea.r = q + F + 1, Ea.j = 0, Ea.t = 1, Ea.Ac = 1, Ea.r2 = q, Ea.j2 = B)
                            }
                        } if (M > O) {
                        M = O;
                        for (E = 0; M > g.m[E]; E += 2);
                        g.m[E] = M;
                        E += 2
                    }
                    if (M >= y) {
                        for (H = I + Jb[g.bb[U] >>> 2]; q + M > A;) g.a[++A].z = 268435455;
                        for (B = 0; y > g.m[B];) B += 2;
                        for (F = y;; ++F)
                            if (u = g.m[B + 1], ta = H + Aa(g, u, F, la), Ea = g.a[q + F], Ea.z > ta && (Ea.z = ta, Ea.r = q, Ea.j = u + 4, Ea.t = 0), F == g.m[B]) {
                                if (S > F && (ca = Math.min(S - 1 - F, g.n), G = Sb(g.b, F, u, ca), 2 <= G)) {
                                    w = 7 > U ? 7 : 10;
                                    Ea = l + F & g.y;
                                    ta = ta + Jb[g.C[(w << 4) + Ea] >>> 2] + J(x(g.A, l + F, Pa(g.b, F - 1 - 1)), 1, Pa(g.b, F - (u + 1) - 1), Pa(g.b, F - 1));
                                    w = xa(w);
                                    Ea = l + F + 1 & g.y;
                                    ta += Jb[2048 - g.C[(w << 4) + Ea] >>> 2];
                                    ta += Jb[2048 - g.bb[w] >>> 2];
                                    for (Ja = F + 1 + G; q + Ja > A;) g.a[++A].z = 268435455;
                                    ta += (pa = g.i.Cc[272 * Ea + (G - 2)], pa + Ia(g, 0, w, Ea));
                                    Ea = g.a[q + Ja];
                                    Ea.z > ta && (Ea.z = ta, Ea.r = q + F + 1, Ea.j = 0, Ea.t = 1, Ea.Ac = 1, Ea.r2 = q, Ea.j2 = u + 4)
                                }
                                if (B += 2, B == E) break
                            }
                    }
                }
            }
        }

        function Aa(g, l, r, t) {
            var q, u = S(r);
            return q = 128 > l ? g.lb[128 * u + l] : g.P[(u << 6) + (131072 > l ? $d[l >> 6] + 12 : 134217728 > l ? $d[l >> 16] + 32 : $d[l >> 26] + 52)] + g.nc[15 &
                l], q + g.$.Cc[272 * t + (r - 2)]
        }

        function Ia(g, l, r, t) {
            var q;
            return l ? (q = Jb[2048 - g.hb[r] >>> 2], 1 == l ? q += Jb[g.Ub[r] >>> 2] : (q += Jb[2048 - g.Ub[r] >>> 2], q += ud(g.vc[r], l - 2))) : (q = Jb[g.hb[r] >>> 2], q += Jb[2048 - g._[(r << 4) + t] >>> 2]), q
        }

        function Na(g) {
            var l = 0;
            a: {
                var q = g.b;
                var r = g.m,
                    t, v, w, x, y, A, B, E;
                if (q.h >= q.o + q.ob) var F = q.ob;
                else if (F = q.h - q.o, q.xb > F) {
                    q = (la(q), 0);
                    break a
                }
                var G = 0;
                var H = q.o > q.p ? q.o - q.p : 0;
                var J = q.f + q.o;
                var L = 1;
                var M = y = 0;q.qb ? (v = fe[255 & q.c[J]] ^ 255 & q.c[J + 1], y = 1023 & v, v ^= (255 & q.c[J + 2]) << 8, M = 65535 & v, A = (v ^ fe[255 & q.c[J +
                    3]] << 5) & q.Ec) : A = 255 & q.c[J] ^ (255 & q.c[J + 1]) << 8;v = q.ub[q.R + A] || 0;q.qb && (t = q.ub[y] || 0, w = q.ub[1024 + M] || 0, q.ub[y] = q.o, q.ub[1024 + M] = q.o, t > H && q.c[q.f + t] == q.c[J] && (r[G++] = L = 2, r[G++] = q.o - t - 1), w > H && q.c[q.f + w] == q.c[J] && (w == t && (G -= 2), r[G++] = L = 3, r[G++] = q.o - w - 1, t = w), 0 != G && t == v && (G -= 2, L = 1));q.ub[q.R + A] = q.o;A = (q.k << 1) + 1;
                var N = q.k << 1;y = M = q.w;0 != q.w && v > H && q.c[q.f + v + q.w] != q.c[J + q.w] && (r[G++] = L = q.w, r[G++] = q.o - v - 1);
                for (t = q.Fc;;) {
                    if (H >= v || 0 == t--) {
                        q.L[A] = q.L[N] = 0;
                        break
                    }
                    if (x = q.o - v, w = (q.k >= x ? q.k - x : q.k - x + q.p) << 1, E = q.f + v, B =
                        M > y ? y : M, q.c[E + B] == q.c[J + B]) {
                        for (; ++B != F && q.c[E + B] == q.c[J + B];);
                        if (B > L && (r[G++] = L = B, r[G++] = x - 1, B == F)) {
                            q.L[N] = q.L[w];
                            q.L[A] = q.L[w + 1];
                            break
                        }
                    }(255 & q.c[J + B]) > (255 & q.c[E + B]) ? (q.L[N] = v, N = w + 1, v = q.L[N], M = B) : (q.L[A] = v, A = w, v = q.L[A], y = B)
                }
                q = (la(q), G)
            }
            return g.D = q, 0 < g.D && (l = g.m[g.D - 2], l == g.n && (l += Sb(g.b, l - 1, g.m[g.D - 1], 273 - l))), ++g.s, l
        }

        function ha(g) {
            g.b && g.W && (g.b.cc = null, g.W = 0)
        }

        function ca(g) {
            return 2048 > g ? $d[g] : 2097152 > g ? $d[g >> 10] + 20 : $d[g >> 20] + 40
        }

        function T(g, l) {
            fb(g.db);
            for (var q = 0; l > q; ++q) fb(g.Vb[q].G), fb(g.Wb[q].G);
            fb(g.ic.G)
        }

        function cd(g, l, r, t, v) {
            var q;
            var u = Jb[g.db[0] >>> 2];
            var w = Jb[2048 - g.db[0] >>> 2];
            var x = w + Jb[g.db[1] >>> 2];
            w += Jb[2048 - g.db[1] >>> 2];
            for (q = 0; 8 > q; ++q) {
                if (q >= r) return;
                t[v + q] = u + Pb(g.Vb[l], q)
            }
            for (; 16 > q; ++q) {
                if (q >= r) return;
                t[v + q] = x + Pb(g.Wb[l], q - 8)
            }
            for (; r > q; ++q) t[v + q] = w + Pb(g.ic, q - 8 - 8)
        }

        function ya(g, l, r, t) {
            8 > r ? (gb(l, g.db, 0, 0), W(g.Vb[t], l, r)) : (r -= 8, gb(l, g.db, 0, 1), 8 > r ? (gb(l, g.db, 1, 0), W(g.Wb[t], l, r)) : (gb(l, g.db, 1, 1), W(g.ic, l, r - 8)));
            0 == --g.sc[t] && (cd(g, t, g.rb, g.Cc, 272 * t), g.sc[t] = g.rb)
        }

        function H(g) {
            g.db =
                A(2);
            g.Vb = A(16);
            g.Wb = A(16);
            g.ic = Cb({}, 8);
            for (var l = 0; 16 > l; ++l) g.Vb[l] = Cb({}, 3), g.Wb[l] = Cb({}, 3);
            return g.Cc = [], g.sc = [], g
        }

        function wb(g, l) {
            for (var q = 0; l > q; ++q) cd(g, q, g.rb, g.Cc, 272 * q), g.sc[q] = g.rb
        }

        function x(g, l, r) {
            return g.V[((l & g.qc) << g.u) + ((255 & r) >>> 8 - g.u)]
        }

        function O(g, l, r) {
            var q, t = 1;
            for (q = 7; 0 <= q; --q) {
                var u = r >> q & 1;
                gb(l, g.tb, t, u);
                t = t << 1 | u
            }
        }

        function J(g, l, r, t) {
            var q, u, v = 1,
                w = 7,
                x = 0;
            if (l)
                for (; 0 <= w; --w)
                    if (u = r >> w & 1, q = t >> w & 1, x += ud(g.tb[(1 + u << 8) + v], q), v = v << 1 | q, u != q) {
                        --w;
                        break
                    } for (; 0 <= w; --w) q = t >> w & 1, x += ud(g.tb[v],
                q), v = v << 1 | q;
            return x
        }

        function w(g, l) {
            return g.F = l, g.G = A(1 << l), g
        }

        function cb(g, l) {
            var q, r = 1;
            for (q = g.F; 0 != q; --q) r = (r << 1) + Hb(l, g.G, r);
            return r - (1 << g.F)
        }

        function Cb(g, l) {
            return g.F = l, g.G = A(1 << l), g
        }

        function W(g, l, r) {
            var q, t = 1;
            for (q = g.F; 0 != q;) {
                --q;
                var u = r >>> q & 1;
                gb(l, g.G, t, u);
                t = t << 1 | u
            }
        }

        function Pb(g, l) {
            var q, r = 1,
                t = 0;
            for (q = g.F; 0 != q;) {
                --q;
                var u = l >>> q & 1;
                t += ud(g.G[r], u);
                r = (r << 1) + u
            }
            return t
        }

        function U(g, l, r) {
            var q, t = 1;
            for (q = 0; g.F > q; ++q) {
                var u = 1 & r;
                gb(l, g.G, t, u);
                t = t << 1 | u;
                r >>= 1
            }
        }

        function Hb(g, l, t) {
            var q, u = l[t];
            return q = (g.E >>> 11) * u, (-2147483648 ^ q) > (-2147483648 ^ g.Bb) ? (g.E = q, l[t] = u + (2048 - u >>> 5) << 16 >> 16, -16777216 & g.E || (g.Bb = g.Bb << 8 | r(g.Ab), g.E <<= 8), 0) : (g.E -= q, g.Bb -= q, l[t] = u - (u >>> 5) << 16 >> 16, -16777216 & g.E || (g.Bb = g.Bb << 8 | r(g.Ab), g.E <<= 8), 1)
        }

        function fb(g) {
            for (var l = g.length - 1; 0 <= l; --l) g[l] = 1024
        }

        function gb(g, l, r, v) {
            var q = l[r];
            var u = (g.E >>> 11) * q;
            v ? (g.xc = t(g.xc, E(M(u), [4294967295, 0])), g.E -= u, l[r] = q - (q >>> 5) << 16 >> 16) : (g.E = u, l[r] = q + (2048 - q >>> 5) << 16 >> 16); - 16777216 & g.E || (g.E <<= 8, ic(g))
        }

        function oc(g, l, r) {
            for (--r; 0 <=
                r; --r) g.E >>>= 1, 1 == (l >>> r & 1) && (g.xc = t(g.xc, M(g.E))), -16777216 & g.E || (g.E <<= 8, ic(g))
        }

        function ic(g) {
            var q = g.xc;
            var r = 32,
                v;
            q = (r &= 63, v = l(q, r), 0 > q[1] && (v = t(v, ea([2, 0], 63 - r))), v);
            r = aa(q);
            if (0 != r || 0 > B(g.xc, [4278190080, 0])) {
                g.mc = t(g.mc, M(g.Jb));
                q = g.Oc;
                do v = g.Ab, q += r, v.Mc[v.Yb++] = q << 24 >> 24, q = 255; while (0 != --g.Jb);
                g.Oc = aa(g.xc) >>> 24
            }++g.Jb;
            g.xc = ea(E(g.xc, [16777215, 0]), 8)
        }

        function ud(g, l) {
            return Jb[(2047 & (g - l ^ -l)) >>> 2]
        }

        function hg(g) {
            for (var l, q, r, t = 0, v = 0, w = g.length, x = [], y = []; w > t; ++t, ++v) {
                if (l = 255 & g[t], 128 & l)
                    if (192 ==
                        (224 & l)) {
                        if (t + 1 >= w || (q = 255 & g[++t], 128 != (192 & q))) return g;
                        y[v] = (31 & l) << 6 | 63 & q
                    } else {
                        if (224 != (240 & l) || t + 2 >= w || (q = 255 & g[++t], 128 != (192 & q)) || (r = 255 & g[++t], 128 != (192 & r))) return g;
                        y[v] = (15 & l) << 12 | (63 & q) << 6 | 63 & r
                    }
                else {
                    if (!l) return g;
                    y[v] = l
                }
                16383 == v && (x.push(String.fromCharCode.apply(String, y)), v = -1)
            }
            return 0 < v && (y.length = v, x.push(String.fromCharCode.apply(String, y))), x.join("")
        }

        function Hd(g) {
            var l, q, r = [],
                t = 0,
                v = g.length;
            if ("object" == typeof g) return g;
            for (q = l = 0; v > q; ++q) r[l++] = g.charCodeAt(q);
            for (q = 0; v > q; ++q) g =
                r[q], 1 <= g && 127 >= g ? ++t : t += !g || 128 <= g && 2047 >= g ? 2 : 3;
            l = [];
            for (q = t = 0; v > q; ++q) g = r[q], 1 <= g && 127 >= g ? l[t++] = g << 24 >> 24 : !g || 128 <= g && 2047 >= g ? (l[t++] = (192 | g >> 6 & 31) << 24 >> 24, l[t++] = (128 | 63 & g) << 24 >> 24) : (l[t++] = (224 | g >> 12 & 15) << 24 >> 24, l[t++] = (128 | g >> 6 & 63) << 24 >> 24, l[t++] = (128 | 63 & g) << 24 >> 24);
            return l
        }

        function ee(g) {
            return g[1] + g[0]
        }
        var Rc = "function" == typeof setImmediate ? setImmediate : setTimeout,
            Ib = 4294967296,
            dd = [4294967295, -Ib],
            ff = [0, -9223372036854775808],
            ta = [0, 0],
            Je = [1, 0],
            fe = function() {
                var g, l, r = [];
                for (g = 0; 256 > g; ++g) {
                    var t =
                        g;
                    for (l = 0; 8 > l; ++l) 0 != (1 & t) ? t = t >>> 1 ^ -306674912 : t >>>= 1;
                    r[g] = t
                }
                return r
            }(),
            $d = function() {
                var g, l, r = 2,
                    t = [0, 1];
                for (l = 2; 22 > l; ++l) {
                    var v = 1 << (l >> 1) - 1;
                    for (g = 0; v > g; ++g, ++r) t[r] = l << 24 >> 24
                }
                return t
            }(),
            Jb = function() {
                var g, l, r = [];
                for (l = 8; 0 <= l; --l) {
                    var t = 1 << 9 - l - 1;
                    for (g = 1 << 9 - l; g > t; ++t) r[t] = (l << 6) + (g - t << 6 >>> 9 - l - 1)
                }
                return r
            }(),
            tc = function() {
                var g = [{
                    s: 16,
                    f: 64,
                    m: 0
                }, {
                    s: 20,
                    f: 64,
                    m: 0
                }, {
                    s: 19,
                    f: 64,
                    m: 1
                }, {
                    s: 20,
                    f: 64,
                    m: 1
                }, {
                    s: 21,
                    f: 128,
                    m: 1
                }, {
                    s: 22,
                    f: 128,
                    m: 1
                }, {
                    s: 23,
                    f: 128,
                    m: 1
                }, {
                    s: 24,
                    f: 255,
                    m: 1
                }, {
                    s: 25,
                    f: 255,
                    m: 1
                }];
                return function(l) {
                    return g[l -
                        1] || g[6]
                }
            }();
        return "undefined" == typeof onmessage || "undefined" != typeof window && void 0 !== window.document || ! function() {
            onmessage = function(g) {
                g && g.gc && (2 == g.gc.action ? e.decompress(g.gc.gc, g.gc.cbn) : 1 == g.gc.action && e.compress(g.gc.gc, g.gc.Rc, g.gc.cbn))
            }
        }(), {
            compress: function(g, l, r, t) {
                function q() {
                    try {
                        for (var g, l = (new Date).getTime(); kb(w.c.yb);)
                            if (u = ee(w.c.yb.Pb) / ee(w.c.Tb), 200 < (new Date).getTime() - l) return t(u), Rc(q, 0), 0;
                        t(1);
                        g = G(w.c.Nb);
                        Rc(r.bind(null, g), 0)
                    } catch (xc) {
                        r(null, xc)
                    }
                }
                var u, v, w = {},
                    x = void 0 ===
                    r && void 0 === t;
                if ("function" != typeof r && (v = r, r = t = 0), t = t || function(g) {
                        void 0 !== v && postMessage({
                            action: 3,
                            cbn: v,
                            result: g
                        })
                    }, r = r || function(g, l) {
                        return void 0 !== v ? postMessage({
                            action: 1,
                            cbn: v,
                            result: g,
                            error: l
                        }) : void 0
                    }, x) {
                    for (w.c = sa({}, Hd(g), tc(l)); kb(w.c.yb););
                    return G(w.c.Nb)
                }
                try {
                    w.c = sa({}, Hd(g), tc(l)), t(0)
                } catch (vd) {
                    return r(null, vd)
                }
                Rc(q, 0)
            },
            decompress: function(g, l, r) {
                function q() {
                    try {
                        for (var g, u = 0, w = (new Date).getTime(); kb(v.d.yb);)
                            if (0 == ++u % 1E3 && 200 < (new Date).getTime() - w) return y && (t = ee(v.d.yb.Z.g) /
                                x, r(t)), Rc(q, 0), 0;
                        r(1);
                        g = hg(G(v.d.Nb));
                        Rc(l.bind(null, g), 0)
                    } catch (Le) {
                        l(null, Le)
                    }
                }
                var t, u, v = {},
                    w = void 0 === l && void 0 === r;
                if ("function" != typeof l && (u = l, l = r = 0), r = r || function(g) {
                        void 0 !== u && postMessage({
                            action: 3,
                            cbn: u,
                            result: y ? g : -1
                        })
                    }, l = l || function(g, l) {
                        return void 0 !== u ? postMessage({
                            action: 2,
                            cbn: u,
                            result: g,
                            error: l
                        }) : void 0
                    }, w) {
                    for (v.d = $a({}, g); kb(v.d.yb););
                    return hg(G(v.d.Nb))
                }
                try {
                    v.d = $a({}, g);
                    var x = ee(v.d.Tb);
                    var y = -1 < x;
                    r(0)
                } catch (fd) {
                    return l(null, fd)
                }
                Rc(q, 0)
            }
        }
    }();
    this.LZMA = this.LZMA_WORKER = e
}
