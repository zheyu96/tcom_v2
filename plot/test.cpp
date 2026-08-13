#include <bits/stdc++.h>
using namespace std;
const double EPS = 1e-9;
const double INF = 1e9;
const double mT = 0.04;
const double A = 0.25, B = 0.75;
double n = 1;
const double tau = 0.2;
double bar(double F) {
    return (1.0 - F);
}
double Fswap(double Fa, double Fb) {
    if(Fa <= A + EPS || Fb <= A + EPS) return 0.25;
    if(Fa >= INF / 2 || Fb >= INF / 2) return 0.25;
    return Fa * Fb + (1.0 / 3.0) * bar(Fa) * bar(Fb);
}
double t2F(double t) {
    if(t >= INF / 1e2) return 0.25;
    return A + B * exp(-pow(t / mT, n));
}
double F2t(double F) {
    if(F <= A + EPS) return INF;
    return mT * pow(-log((F - A) / B), 1.0 / n);
}
double pass_tau(double F) {
    if(F >= INF / 1e2) return INF;
    return t2F(F2t(F) + tau);
}



double sk_tree(double F1, double F2, double F3, double F4) {
    double F5 = Fswap(pass_tau(F1), pass_tau(F2));
    double F6 = Fswap(pass_tau(F5), pass_tau(F3));
    double F7 = Fswap(pass_tau(F6), pass_tau(F4));
    return F7;
}
double bl_tree(double F1, double F2, double F3, double F4) {
    double F5 = Fswap(pass_tau(F1), pass_tau(F2));
    double F6 = Fswap(pass_tau(F3), pass_tau(F4));
    double F7 = Fswap(pass_tau(F5), pass_tau(F6));
    return F7;
}

double sk_tree(vector<double> Fs) {
    if(Fs.size() == 0LL) return 1; 
    if(Fs.size() == 1LL) return Fs[0]; 
    double F = Fswap(pass_tau(Fs[0]), pass_tau(Fs[1]));
    for(int i = 2; i < (int)Fs.size(); i++) {
        F = Fswap(pass_tau(F), pass_tau(Fs[i]));
    }
    return F;
}


double bl_tree(vector<double> Fs) {
    if(Fs.size() == 0LL) return 1; 
    while(Fs.size() >= 2) {
        vector<double> Fsnew;
        for(int i = 0; i < (int)Fs.size(); i += 2) {
            if(i + 1 < (int)Fs.size()) Fsnew.push_back(Fswap(pass_tau(Fs[i]), pass_tau(Fs[i + 1])));
            else Fsnew.push_back(Fs[i]);
        }
        Fs = Fsnew;
    }
    return Fs[0];
}


int main() {
    // random_device rd;
    // mt19937 gen(rd());
    // uniform_real_distribution<double> dis(0.25, 1);
    // do {
    //     int n = 10;
    //     vector<double> Fs(n);
    //     // vector<double> Fs = {0.98, 0.98, 0.95, 0.9};
    //     for(int i = 0; i < n; i++) Fs[i] = dis(gen);
    //     cerr << "F_init: " << endl;
    //     for(auto F : Fs) {
    //         cerr << F << " ";
    //     }
    //     cerr << "\n";

    //     double res_sk = sk_tree(Fs), res_bl = bl_tree(Fs);
    //     cerr << "result: " << res_sk << " " << res_bl << endl;
    //     if(fabs(res_sk - res_bl) > EPS) {
    //         cout << res_sk << " " << res_bl << endl;
    //         exit(0);
    //     }
    // } while(1);
    for(n = 1; n <= 3.05; n += 0.1) {
        vector<double> Fs = {0.98, 0.98, 0.95, 0.9};
        double res_sk = sk_tree(Fs[0], Fs[1], Fs[2], Fs[3]);
        double res_bl = bl_tree(Fs[0], Fs[1], Fs[2], Fs[3]);
        cout << n << " " << res_sk << " " << res_bl << endl;
    }
}
