package org.hootina.platform.services;

import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserFactory;

public class ParserByPULL {

	// ����XmlPullParser������XML�ļ�
	public static List<Face> getStudents(InputStream inStream) throws Throwable {
		List<Face> faces = null;
		Face mface = null;

		XmlPullParserFactory pullFactory = XmlPullParserFactory.newInstance();
		XmlPullParser parser = pullFactory.newPullParser();
		// ��ʽ��:ʹ��Android�ṩ��ʵ�ù�����android.util.Xml
		// XmlPullParser parser = Xml.newPullParser();

		// �����ļ�������
		parser.setInput(inStream, "UTF-8");
		// ������һ���¼�
		int eventType = parser.getEventType();
		// ֻҪ�����ĵ������¼�����һֱѭ��
		while (eventType != XmlPullParser.END_DOCUMENT) {
			switch (eventType) {
			// ������ʼ�ĵ��¼�
			case XmlPullParser.START_DOCUMENT:
				faces = new ArrayList<Face>();
				break;
			// ������ʼԪ���¼�
			case XmlPullParser.START_TAG:
				// ��ȡ��������ǰָ���Ԫ�ص�����
				String name = parser.getName();
				if ("face".equals(name)) {
					// ͨ����������ȡid��Ԫ��ֵ��������student��id
					mface = new Face();

					mface.setFaceid(parser.getAttributeValue(0));
					mface.setFile(parser.getAttributeValue(1));
					mface.setTip(parser.getAttributeValue(2));

				}

				break;
			// ��������Ԫ���¼�
			case XmlPullParser.END_TAG:
				//
				if ("face".equals(parser.getName())) {
					faces.add(mface);
					mface = null;
				}
				break;
			default:
				break;
			}
			eventType = parser.next();
		}
		return faces;
	}

}
