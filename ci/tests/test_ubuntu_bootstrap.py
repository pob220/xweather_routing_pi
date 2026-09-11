from pathlib import Path
import unittest

CI = Path(__file__).parents[1]


class UbuntuBootstrap(unittest.TestCase):
    def test_tls_trust_precedes_dependency_download(self):
        for name in ('Dockerfile.jammy', 'Dockerfile.linux'):
            text = (CI / name).read_text()
            self.assertLess(text.index('COPY ubuntu-archive-ca.crt'),
                            text.index('RUN /usr/local/bin/install-debian-build-deps.sh'))
            self.assertIn('s|http://|https://|g', text)
            self.assertIn('COPY apt-network.conf', text)
            self.assertNotIn('Verify-Peer', text)
            self.assertNotIn('RUN apt-get update', text)

    def test_bounded_bootstrap_and_readable_logs(self):
        text = (CI / 'circleci-build-ubuntu-docker.sh').read_text()
        self.assertIn('timeout 15m docker build --progress=plain', text)
        self.assertIn('/etc/ssl/certs/ca-certificates.crt', text)
        policy = (CI / 'apt-network.conf').read_text()
        self.assertIn('Acquire::https::Timeout "20"', policy)
        self.assertIn('Acquire::Retries "2"', policy)
        self.assertIn('APT::Update::Error-Mode "any"', policy)
