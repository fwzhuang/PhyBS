#include "TetMesh.h"


TetMesh::TetMesh()
{
	//InitModel();
	std::cout << Eigen::Matrix3d::Identity();
}


TetMesh::~TetMesh()
{
}

void TetMesh::LoadNodesFromFile(QString filename)
{
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		std::cerr << "Cannot load nodes file." << std::endl;
		exit(0);
	}
		
	QTextStream fin(&file);
	if (!fin.atEnd())
	{
		QString line = fin.readLine().simplified();
		QStringList segs = line.split(" ");
		n_nodes = segs[0].toInt();
		
		//load nodes' positions
		int prefix;
		m_nodes = Eigen::MatrixXd::Zero(3, n_nodes);
		for (size_t i = 0; i < n_nodes; ++i)
		{
			fin >> prefix >> m_nodes(0, i) >> m_nodes(1, i) >> m_nodes(2, i);
		}


		// mass will be loaded from outer file in later version, this is just for quick test
		m_nodes_mass = Eigen::VectorXd::Ones(n_nodes) * 0.1;
		m_nodes_gravity = Eigen::MatrixXd::Zero(3, n_nodes);
		m_nodes_gravity.row(1) = -9.8 * m_nodes_mass.transpose();

		//std::cout << m_nodes;

	}
}

void TetMesh::LoadTetsFromFile(QString filename)
{
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		std::cerr << "Cannot load nodes file." << std::endl;
		exit(0);
	}
	QTextStream fin(&file);
	if (!fin.atEnd())
	{
		QString line = fin.readLine().simplified();
		QStringList segs = line.split(" ");
		n_tets = segs[0].toInt();

		//load Tets' indices
		int prefix;
		m_tets = Eigen::MatrixXi::Zero(4, n_tets);
		for (size_t i = 0; i < n_tets; ++i)
		{
			fin >> prefix >> m_tets(0, i) >> m_tets(1, i) >> m_tets(2, i) >> m_tets(3, i);
		}
		//std::cout << m_tets;
	}
}

void TetMesh::LoadFacesFromFile(QString filename)
{
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		std::cerr << "Cannot load nodes file." << std::endl;
		exit(1);
	}
	QTextStream fin(&file);

	if (!fin.atEnd())
	{
		QString line = fin.readLine().simplified();
		QStringList segs = line.split(" ");
		n_bound_faces = segs[0].toInt();

		//load Tets' indices
		int prefix;
		m_bound_faces = Eigen::MatrixXi::Zero(3, n_bound_faces);
		for (size_t i = 0; i < n_tets; ++i)
		{
			fin >> prefix >> m_bound_faces(0,i) >> m_bound_faces(1,i) >> m_bound_faces(2,i);
		}
		//std::cout << m_bound_faces;
	}
}

void TetMesh::InitModel()
{
	LoadNodesFromFile(QStringLiteral("..\\model\\box\\box.1.node"));
	LoadTetsFromFile(QStringLiteral("..\\model\\box\\box.1.ele"));
	LoadFacesFromFile(QStringLiteral("..\\model\\box\\box.1.face"));

	m_Dm_inverses = Eigen::MatrixXd::Zero(3, n_tets * 3);
	m_ANs		  = Eigen::MatrixXd::Zero(3, n_tets * 3);

	// precompute dim_inverse for each tetrahedron and bi for three nodes in each tetrahedron
	for (size_t i = 0; i < n_tets; ++i)
	{
		Eigen::Matrix3d Dm = Eigen::Matrix3d::Zero();
		Dm.col(0) = (m_nodes.col(m_tets(1, i)) - m_nodes.col(m_tets(0, i)));
		Dm.col(1) = (m_nodes.col(m_tets(2, i)) - m_nodes.col(m_tets(0, i)));
		Dm.col(2) = (m_nodes.col(m_tets(3, i)) - m_nodes.col(m_tets(0, i)));
		m_Dm_inverses.block<3,3>(0,i*3) = Dm.inverse();

		ComputeANs(i);
	}	

	std::cout << "tet model has been initialized."<<std::endl;
}

void TetMesh::ComputeANs(int tetid)
{
	int edge[3][4] = {{1,0,2,3},{2,0,1,3},{3,0,1,2}};
	Eigen::Vector3d v1, v2, v3;
	for (size_t i = 0; i < 3; ++i)
	{
		v1 = m_nodes.col(m_tets(edge[i][1], tetid)) - m_nodes.col(m_tets(edge[i][0], tetid));
		v2 = m_nodes.col(m_tets(edge[i][2], tetid)) - m_nodes.col(m_tets(edge[i][0], tetid));
		v3 = m_nodes.col(m_tets(edge[i][3], tetid)) - m_nodes.col(m_tets(edge[i][0], tetid));

		// Explanation here: the following line computes b_i = (-1/3)(A_1*N_1+A_2*N_2+A_3*N_3)
		// Note that cross product of two edges is twice the area of a face times the normal,
		// So we can simply add one sixth of -sigma times the cross product to each of the three nodes.
		m_ANs.col(tetid * 3 + i) = -(v1.cross(v2) + v2.cross(v3) + v3.cross(v1)) / 6.0;
	}
}

void TetMesh::ComputeForces()
{
	m_nodes_forces = m_nodes_gravity;
	Eigen::Matrix3d Ds;
	Eigen::Matrix3d F;									// Ds * Dm_inverse
	Eigen::Matrix3d G;									// Green Strain 1/2 * (F_tanspose * F - I)
	Eigen::Matrix3d sigma = Eigen::Matrix3d::Zero();	// Cauch stress using isotropic linear elastic material
	Eigen::Matrix3d P;									// First Piola-Kirchhoff stress
	for (int i = 0; i < n_tets; ++i)
	{
		Ds.col(0) = m_nodes.col(m_tets(1, i)) - m_nodes.col(m_tets(0, i));
		Ds.col(1) = m_nodes.col(m_tets(2, i)) - m_nodes.col(m_tets(0, i));
		Ds.col(2) = m_nodes.col(m_tets(3, i)) - m_nodes.col(m_tets(0, i));
		
		F = Ds * m_Dm_inverses.block<3,3>(0,3*i);
		G = (F.transpose()*F - Eigen::Matrix3d::Identity()) / 2.0;

		sigma(0, 0) = G(0, 0) *m_Enu3 + G(1, 1)*m_Enu2 + G(2, 2)*m_Enu2;
		sigma(1, 1) = G(0, 0) *m_Enu2 + G(1, 1)*m_Enu3 + G(2, 2)*m_Enu2;
		sigma(2, 2) = G(0, 0) *m_Enu2 + G(1, 1)*m_Enu2 + G(2, 2)*m_Enu3;

		sigma(1, 2) = sigma(2, 1) = G(1, 2) * m_Enu1;
		sigma(0, 2) = sigma(2, 0) = G(0, 2) * m_Enu1;
		sigma(0, 1) = sigma(1, 0) = G(0, 1) * m_Enu1;

		// Cauchy stress to First Piola-Kirchhoff stress
		P = F.determinant() * sigma * F.transpose().inverse();
		// compute forces for node1, node2 and node3.  f4 = - (f1+f2+f3)
		Eigen::Matrix3d inner_forces = P * m_ANs.block<3, 3>(0, 3 * i);     
		m_nodes_forces.col(m_tets(1, i)) += inner_forces.col(0);
		m_nodes_forces.col(m_tets(2, i)) += inner_forces.col(1);
		m_nodes_forces.col(m_tets(3, i)) += inner_forces.col(2);
		m_nodes_forces.col(m_tets(0, i)) -= (inner_forces.col(0)+inner_forces.col(1)+inner_forces.col(2));
	}
}

void TetMesh::SetTetMaterial(double e, double nu)
{
	m_E  = e;
	m_nu = nu;

	m_Enu1 = m_E / (1 + m_nu);
	m_Enu2 = m_Enu1 * m_nu / (1- m_nu * 2);
	m_Enu3 = m_Enu1 * (1 - m_nu) / (1 - m_nu * 2);
}