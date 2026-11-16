clear; clc;
Vs = 5; Rs = 1e3; r = 1e3; c = 1e-6;       % scaled model
d  = 0.5:0.5:5;                            % true fault distance (km)

% Short circuit: voltage -> distance
V   = Vs*(r*d)./(Rs + r*d);
lsb = Vs/4095;                             % 12-bit ADC over 0-5 V
Vm  = round((V + 0.005*randn(size(V)))/lsb)*lsb;   % 5 mV noise + quantisation
dE  = (Vm*Rs)./(r*(Vs - Vm));
err = abs(dE - d)./d*100;

figure;
subplot(1,3,1); plot(d,V,'-o'); grid on; xlabel('Fault distance (km)'); ylabel('Cable voltage (V)'); title('V vs distance');
subplot(1,3,2); plot(d,d,'k--',d,dE,'ro'); grid on; xlabel('True (km)'); ylabel('Estimated (km)'); title('Localisation'); legend('ideal','estimated','Location','best');
subplot(1,3,3); bar(d,err); grid on; xlabel('True (km)'); ylabel('Error (%)'); title('Localisation error');

% Open circuit: RC charging -> distance
t = 0:1e-5:0.03; figure; hold on;
for k = 1:5
    plot(t*1e3, Vs*(1-exp(-t/(Rs*c*k))));
end
grid on; xlabel('Time (ms)'); ylabel('Cable voltage (V)'); title('Open-circuit charging, 1-5 km');
legend('1 km','2 km','3 km','4 km','5 km');
